/*
 * Copyright (C) 2015-2021 IoT.bzh Company
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

#pragma once

// main config structure
struct optargs {
	char *rootdir;		// base dir for files
	char *roothttp;		// directory for http files
	char *rootbase;		// Angular HTML5 base URL
	char *rootapi;		// Base URL for REST APIs
	char *workdir;		// where to run the program
	char *uploaddir;	// where to store transient files
	char *name;		/* name to set to the daemon */
	char *ws_server;	/* exported api */

	/* integers */
	int httpdPort;
	int cacheTimeout;
	int apiTimeout;
	int cntxTimeout;	// Client Session Context timeout
	int nbSessionMax;	// max count of sessions
};

extern struct optargs *optargs_parse(int argc, char **argv);

