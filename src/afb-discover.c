/*
 * Copyright (C) 2015-2022 IoT.bzh Company
 * Author: José Bollo <jose.bollo@iot.bzh>
 *
 * $RP_BEGIN_LICENSE$
 * Commercial License Usage
 *  Licensees holding valid commercial IoT.bzh licenses may use this file in
 *  accordance with the commercial license agreement provided with the
 *  Software or, alternatively, in accordance with the terms contained in
 *  a written agreement between you and The IoT.bzh Company. For licensing terms
 *  and conditions see https://www.iot.bzh/terms-conditions. For further
 *  information use the contact form at https://www.iot.bzh/contact.
 * 
 * GNU General Public License Usage
 *  Alternatively, this file may be used under the terms of the GNU General
 *  Public license version 3. This license is as published by the Free Software
 *  Foundation and appearing in the file LICENSE.GPLv3 included in the packaging
 *  of this file. Please review the following information to ensure the GNU
 *  General Public License requirements will be met
 *  https://www.gnu.org/licenses/gpl-3.0.html.
 * $RP_END_LICENSE$
 */


#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>

void afs_discover(const char *pattern, void (*callback)(void *closure, pid_t pid), void *closure)
{
	intmax_t n;
	DIR *dir;
	struct dirent *ent;
	char *name;
	char exe[PATH_MAX], lnk[PATH_MAX];

	dir = opendir("/proc");
	while ((ent = readdir(dir))) {
		name = ent->d_name;
		while (isdigit(*name))
			name++;
		if (*name)
			continue;
		n = snprintf(exe, sizeof exe, "/proc/%s/exe", ent->d_name);
		if (n < 0 || (size_t)n >= sizeof exe)
			continue;
		n = readlink(exe, lnk, sizeof lnk);
		if (n < 0 || (size_t)n >= sizeof lnk)
			continue;
		lnk[n] = 0;
		name = lnk;
		while(*name) {
			while(*name == '/')
				name++;
			if (*name) {
				if (!strcmp(name, pattern)) {
					callback(closure, (pid_t)atoi(ent->d_name));
					break;
				}
				while(*++name && *name != '/');
			}
		}
	}
	closedir(dir);
}

