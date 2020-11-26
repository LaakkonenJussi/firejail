/*
 * Copyright (C) 2014-2020 Firejail Authors
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

#ifndef EUID_COMMON_H
#define EUID_COMMON_H
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>
#include <pwd.h>

/* Sailfish OS uses privileged user/group file ownership
 * to limit access to data with privacy implications and
 * this must be taken into account during sandbox setup.
 *
 * If such user/group does not exist, all features related
 * to privileged data should be automatically disabled.
 */
#define PRIVILEGED_USER  "privileged"
#define PRIVILEGED_GROUP "privileged"

#define INVALID_UID ((uid_t)(-1))
#define INVALID_GID ((gid_t)(-1))

#define EUID_ASSERT() { \
	if (getuid() != 0) \
		assert(geteuid() != 0); \
}

typedef struct {
  uid_t uid;
  gid_t gid;
  gid_t primary_gid;
  gid_t privileged_gid;
  uid_t privileged_uid;
} euid_data_t;

#define EUID_DATA_INIT  {\
    .uid            = 0,\
    .gid            = 0,\
    .primary_gid    = INVALID_GID,\
    .privileged_gid = INVALID_GID,\
    .privileged_uid = INVALID_UID,\
}
extern euid_data_t euid_data;
extern int arg_debug;

/* Implement as macros so that error reporting refers
 * to call site instead of this header file ... */
#define EUID_ROOT() do {\
	if (seteuid(0) == -1)\
		errExit("EUID_ROOT:seteuid(root)");\
	if (setegid(0) == -1)\
		errExit("EUID_ROOT:setegid(root)");\
} while (0)

#define EUID_USER() do {\
	if (seteuid(euid_data.uid) == -1)\
		errExit("EUID_USER:seteuid(user)");\
	if (setegid(euid_data.gid) == -1)\
		errExit("EUID_USER:setegid(user)");\
} while (0)

static inline void EUID_INIT(const char *progname) {
	struct passwd *pw;

	euid_data.uid = getuid();
	euid_data.gid = getegid();

	if ((pw = getpwuid(euid_data.uid))) {
		if (euid_data.gid != pw->pw_gid)
			euid_data.primary_gid = pw->pw_gid;
	}

	if ((pw = getpwnam(PRIVILEGED_USER))) {
		euid_data.privileged_uid = pw->pw_uid;
		euid_data.privileged_gid = pw->pw_gid;
	}
	if (arg_debug) {
		fprintf(stderr, "%s: uid=%d gid=%d primary_gid=%d privileged_uid=%d privileged_gid=%d\n",
			progname,
			(int)euid_data.uid,
			(int)euid_data.gid,
			(int)euid_data.primary_gid,
			(int)euid_data.privileged_uid,
			(int)euid_data.privileged_gid);
	}
}

#endif
