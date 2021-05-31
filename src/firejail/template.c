/*
 * Copyright (C) 2021 Jolla Ltd.
 * Copyright (C) 2021 Open Mobile Platform
 *
 * This file is part of firejail project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "firejail.h"
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#define TEMPLATE_KEY_VALUE_DELIM ":"
#define TEMPLATE_KEY_MACRO_DELIM "$"
#define TEMPLATE_KEY_MACRO_SUB_DELIMS "{}"
#define TEMPLATE_STR_COMPAT_CHARS "_-/."

typedef struct template_t {
	char *key;
	char *value;
	struct template_t *next;
} Template;

typedef enum {
	STR_CHECK_ALNUM = 0,
	STR_CHECK_COMPAT
} StrCheckType;

Template *tmpl_list = NULL;

static Template *template_new(const char *key, const char *value)
{
	Template *tmpl;

	if (!key || !*key || !value || !*value)
		return NULL;

	tmpl = malloc(sizeof(Template));
	if (!tmpl)
		errExit("malloc");

	tmpl->key = strdup(key);
	tmpl->value = strdup(value);
	tmpl->next = NULL;

	if (arg_debug)
		fprintf(stdout, "Create template key \"%s\" value \"%s\"\n",
								key, value);

	return tmpl;
}

static void template_free(Template *tmpl)
{
	if (!tmpl)
		return;

	if (arg_debug)
		fprintf(stdout, "free %p key \"%s\" value \"%s\"\n", tmpl,
						tmpl->key, tmpl->value);

	free(tmpl->key);
	free(tmpl->value);
	free(tmpl);
}

/*
 * Get template with matching key, if list is empty or key is not found
 * -ENOKEY is set to errno. With empty key -EINVAL is set.
 */
static Template* template_get(const char *key)
{
	Template *iter;

	if (!key || !*key) {
		errno = EINVAL;
		return NULL;
	}

	iter = tmpl_list;
	while (iter) {
		if (!strcmp(key, iter->key))
			return iter;

		iter = iter->next;
	}

	errno = ENOKEY;
	return NULL;
}

/* Return value for a key, errno is set by template_get() with NULL return. */
static const char* template_get_value(const char *key)
{
	Template *tmpl;

	tmpl = template_get(key);
	if (!tmpl)
		return NULL;

	return tmpl->value;
}

/*
 * Prepend template to the list. If the key already exists -EALREADY is
 * reported back and caller must free the Template.
 */
static int template_add(Template *tmpl)
{
	if (!tmpl)
		return -EINVAL;

	if (tmpl_list && template_get(tmpl->key))
		return -EALREADY;

	tmpl->next = tmpl_list;
	tmpl_list = tmpl;

	return 0;
}

/* Free all the Templates in the list */
void template_cleanup()
{
	Template *iter;
	Template *curr;

	iter = tmpl_list;
	while (iter) {
		curr = iter;
		iter = iter->next;
		template_free(curr);
	}

	tmpl_list = NULL;
}

/* For debugging, traverses Template list and prints out keys and values */
void template_print_all()
{
	Template *iter;

	if (!arg_debug)
		return;

	iter = tmpl_list;
	while (iter) {
		fprintf(stdout, "template key \"%s\" value \"%s\"\n",
						iter->key, iter->value);
		iter = iter->next;
	}
}

static int is_compat_char(const char c)
{
	int i;

	for (i = 0 ; TEMPLATE_STR_COMPAT_CHARS[i]; i++) {
		if (c == TEMPLATE_STR_COMPAT_CHARS[i])
			return 1;
	}
	return 0;
}

/* Check if the string is valid for the given type */
static int is_valid_str(const char *str, StrCheckType type)
{
	int i;

	if (!str || !*str)
		return 0;

	// Keys must start with an alphabetic char and the values must not
	// exceed D-Bus limit.
	switch (type) {
	case STR_CHECK_ALNUM:
		if (!isalpha(*str))
			return 0;

		break;
	case STR_CHECK_COMPAT:
		if (strlen(str) > DBUS_MAX_NAME_LENGTH)
			return 0;

		if (strstr(str, ".."))
			return 0;

		break;
	}

	for (i = 1; str[i]; i++) {
		if (iscntrl(str[i]))
			return 0;

		switch (type) {
		case STR_CHECK_ALNUM:
			if (!isalnum(str[i]))
				return 0;

			break;
		case STR_CHECK_COMPAT:
			if (!isalnum(str[i]) && !is_compat_char(str[i]))
				return 0;

			break;
		}
	}

	return 1;
}

/* TODO There should be a function in macro.c to check if a key is internal */
const char *internal_keys[] = { "HOME", "CFG", "RUNUSER", "PATH", "PRIVILEGED",
				NULL };

/* Check if the key is in internal key list or it has a hardcoded macro. */
static int is_internal_macro(const char *key)
{
	char *macro;
	int i;

	for (i = 0; internal_keys[i]; i++) {
		if (!strcmp(key, internal_keys[i]))
			return 1;
	}

	if (asprintf(&macro, "${%s}", key) == -1)
		errExit("asprintf");

	i = macro_id(macro);
	free(macro);

	if (i != -1)
		return 1;

	return 0;
}

/*
 * Check the Template argument to have KEY:VALUE in valid format. A valid
 * Template is added to template list. In case of invalid key, value, internal
 * macro or existing key firejail is called to exit.
 */
void check_template(char *arg) {
	Template *tmpl;
	const char *key;
	const char *value;
	const char *delim = TEMPLATE_KEY_VALUE_DELIM;
	char *saveptr;
	int err;

	/* Only alphanumeric chars in template key. */
	key = strtok_r(arg, delim, &saveptr);
	if (!is_valid_str(key, STR_CHECK_ALNUM)) {
		fprintf(stderr, "Error invalid template key \"%s\"\n", key);
		exit(1);
	}

	/* Only a-zA-Z0-9_ /*/
	value = strtok_r(NULL, delim, &saveptr);
	if (!is_valid_str(value, STR_CHECK_COMPAT)) {
		fprintf(stderr, "Error invalid template value in \"%s:%s\"\n",
								key, value);
		exit(1);
	}

	/* Hardcoded macro or XDG value is not allowed to be overridden. */
	if (is_internal_macro(key)) {
		fprintf(stderr, "Error override of \"${%s}\" is not permitted\n",
									key);
		exit(1);
	}

	/* Returns either a Template or exits firejail */
	tmpl = template_new(key, value);

	err = template_add(tmpl);
	switch (err) {
	case 0:
		return;
	case -EINVAL:
		fprintf(stderr, "Error invalid template key \"%s\" "
						"value \"%s\"\n", key, value);
		break;
	case -EALREADY:
		fprintf(stderr, "Error template key \"%s\" already exists\n",
								key);
		break;
	}

	template_free(tmpl);
	exit(1);
}

/*
 * Check if the argument contains template keys that should be expanded. Will
 * return 1 only when there is at least one template key found. If an unknown
 * template exists -EINVAL is returned.  If there is no '$' or the macros are
 * internal only 0 is returned as there is nothing to expand.
 */
int template_requires_expansion(char *arg)
{
	char *ptr;

	if (!arg || !*arg)
		return 0;

	ptr = strchr(arg, '$');
	if (!ptr)
		return 0;

	while (*ptr) {
		if (*ptr == '$' && *(ptr+1) == '{') {
			char buf[DBUS_MAX_NAME_LENGTH] = { 0 };
			int i;

			// Copy template key name only
			for (i = 0, ptr += 2; *ptr && *ptr != '}' &&
						i < DBUS_MAX_NAME_LENGTH;
						ptr++, i++)
				buf[i] = *ptr;

			if (is_internal_macro(buf))
				continue;

			// Invalid line if '${}' used but no valid template key
			if (!template_get(buf))
				return -EINVAL;

			// At least one template key, needs template expansion
			return 1;
		}
		++ptr;
	}

	return 0;
}

/*
 * Concatenate str1 and str2 by reallocating str1 to fit both. Returns NULL
 * if realloc() fails. Duplicates str2 if str1 is NULL.
 */
static char* append_to_string(char *str1, const char *str2)
{
	size_t len;

	if (!str2)
		return str1;

	if (!str1)
		return strdup(str2);

	len = strlen(str2);
	str1 = realloc(str1, strlen(str1) + len + 1);
	if (!str1)
		return NULL;

	return strncat(str1, str2, len);
}

/*
 * Replace the key with corresponding value in the str_in token, this is called
 * only from template_replace_keys() to process the str_in between '{' and '}'
 * since the line is tokenized first using '$'. With strtok_r() the '{' and '}'
 * are replaced using as delimiters and only the first part of the str_in is
 * the actual template key, which is replaced, rest is appended to the
 * container. If the key is an internal macro it is added to container as
 * '${MACRO_NAME}'. In case of error errno is set to EINVAL unless already
 * being set by realloc() in append_to_string() or template_get() in
 * template_get_value().
 */
static char *process_key_value(char *container, char *str_in)
{
	char *str;
	char *token;
	char *saveptr;
	const char *delim = TEMPLATE_KEY_MACRO_SUB_DELIMS;
	const char *value;

	errno = 0;

	for (str = str_in; ; str = NULL) {
		token = strtok_r(str, delim, &saveptr);
		if (!token)
			break;

		if (is_internal_macro(token)) {
			char *macro;

			if (asprintf(&macro, "${%s}", token) == -1)
				errExit("asprintf");

			container = append_to_string(container, macro);
			free(macro);

			if (!container)
				goto err;

			continue;
		}

		// Only the first iteration is the template key to be expanded
		// and everything after the first token is added to the end.
		value = str ? template_get_value(token) : token;
		if (!value)
			goto err;

		container = append_to_string(container, value);
		if (!container)
			goto err;
	}

	return container;

err:
	if (container)
		free(container);
	else if (!errno)
		errno = EINVAL;

	return NULL;
}

/*
 * Allocates new string with all template keys replaced with the values.
 * Returns NULL if there is a nonexistent key, allocation fails or if arg
 * begins with $. If arg does not contain $ it is only duplicated. Calls
 * process_key_value to replace the template keys with corresponding values.
 * If there are errors (invalid or missing keys) appropriate error is printed
 * and errno is set accordingly by called functions (process_key_value() or
 * append_to_string()).
 */
char *template_replace_keys(char *arg)
{
	char *new_string = NULL;
	char *str;
	char *token;
	char *saveptr;
	const char *delim = TEMPLATE_KEY_MACRO_DELIM;

	if (!arg || !*arg)
		return NULL;

	if (!strchr(arg, '$'))
		return strdup(arg);

	// Templates must not be given at the beginning of the line
	if (*arg == '$') {
		fprintf(stderr, "Error line \"%s\" starts with \"$\"\n", arg);
		return NULL;
	}

	for (str = arg; ; str = NULL) {
		token = strtok_r(str, delim, &saveptr);
		if (!token)
			break;

		/*
		 * Process template values starting from the second token as
		 * templates cannot be used at the beginning of the arg
		 * because only hardcoded macros should be as first.
		 */
		if (!str) {
			// Valid token must begin with '{' and to have '}'
			if (*token != '{' && !strchr(token+1, '}')) {
				if (new_string)
					free(new_string);

				fprintf(stderr, "Unterminated macro/template "
							"key on line \"%s\"\n",
							arg);
				return NULL;
			}

			new_string = process_key_value(new_string, token);
		} else {
			new_string = append_to_string(new_string, token);
		}

		if (!new_string) {
			fprintf(stderr, "Error invalid line \"%s\" (err %s)\n",
							arg, strerror(errno));
			return NULL;
		}
	}

	return new_string;
}
