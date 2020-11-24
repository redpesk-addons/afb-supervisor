/*
 * Copyright (C) 2015-2020 IoT.bzh Company
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
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <libafb/libafb-config.h>

#if WITH_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

#include <libafb/core/afb-common.h>
#include <libafb/core/afb-apiset.h>
#include <libafb/core/afb-session.h>

#if WITH_LIBMICROHTTPD
#include <libafb/http/afb-hsrv.h>
#include <libafb/http/afb-hswitch.h>
#include <libafb/http/afb-hreq.h>
#endif

#include <libafb/apis/afb-api-ws.h>

#include "afb-supervisor-api.h"
#include "afb-supervisor-opts.h"

#include <libafb/sys/verbose.h>
#include <libafb/core/afb-sched.h>
#include <libafb/sys/process-name.h>
#include <libafb/misc/afb-watchdog.h>

#if !defined(DEFAULT_SUPERVISOR_INTERFACE)
#  define DEFAULT_SUPERVISOR_INTERFACE NULL
#endif

/* the main config */
struct optargs *main_config;

/* the main apiset */
struct afb_apiset *main_apiset;

/*************************************************************************************/

#if WITH_LIBMICROHTTPD
static int init_http_server(struct afb_hsrv *hsrv)
{
	if (!afb_hsrv_add_handler
	    (hsrv, main_config->rootapi, afb_hswitch_websocket_switch, main_apiset, 20))
		return 0;

	if (!afb_hsrv_add_handler
	    (hsrv, main_config->rootapi, afb_hswitch_apis, main_apiset, 10))
		return 0;

	if (main_config->roothttp != NULL) {
		if (!afb_hsrv_add_alias
		    (hsrv, "", afb_common_rootdir_get_fd(), main_config->roothttp,
		     -10, 1))
			return 0;
	}

	if (!afb_hsrv_add_handler
	    (hsrv, main_config->rootbase, afb_hswitch_one_page_api_redirect, NULL,
	     -20))
		return 0;

	return 1;
}

static struct afb_hsrv *start_http_server()
{
	int rc;
	struct afb_hsrv *hsrv;

	if (afb_hreq_init_download_path(main_config->uploaddir)) {
		ERROR("unable to set the upload directory %s", main_config->uploaddir);
		return NULL;
	}

	hsrv = afb_hsrv_create();
	if (hsrv == NULL) {
		ERROR("memory allocation failure");
		return NULL;
	}

	if (!afb_hsrv_set_cache_timeout(hsrv, main_config->cacheTimeout)
	    || !init_http_server(hsrv)) {
		ERROR("initialisation of httpd failed");
		afb_hsrv_put(hsrv);
		return NULL;
	}

	NOTICE("Waiting port=%d rootdir=%s", main_config->httpdPort, main_config->rootdir);
	NOTICE("Browser URL= http://localhost:%d", main_config->httpdPort);

	rc = afb_hsrv_start(hsrv, 15);
	if (!rc) {
		ERROR("starting of httpd failed");
		afb_hsrv_put(hsrv);
		return NULL;
	}

	rc = afb_hsrv_add_interface_tcp(hsrv, DEFAULT_SUPERVISOR_INTERFACE, (uint16_t) main_config->httpdPort);
	if (rc < 0) {
		ERROR("setting interface failed");
		afb_hsrv_put(hsrv);
		return NULL;
	}

	return hsrv;
}
#endif

static void start(int signum, void *arg)
{
	struct afb_hsrv *hsrv;
	int rc;

	/* check illness */
	if (signum) {
		ERROR("start aborted: received signal %s", strsignal(signum));
		exit(1);
	}

	/* set the directories */
	mkdir(main_config->workdir, S_IRWXU | S_IRGRP | S_IXGRP);
	if (chdir(main_config->workdir) < 0) {
		ERROR("Can't enter working dir %s", main_config->workdir);
		goto error;
	}
	if (afb_common_rootdir_set(main_config->rootdir) < 0) {
		ERROR("failed to set common root directory");
		goto error;
	}

	/* configure the daemon */
	if (afb_session_init(main_config->nbSessionMax, main_config->cntxTimeout)) {
		ERROR("initialisation of session manager failed");
		goto error;
	}

	main_apiset = afb_apiset_create("main", main_config->apiTimeout);
	if (!main_apiset) {
		ERROR("can't create main apiset");
		goto error;
	}

	/* init the main apiset */
	rc = afs_supervisor_add(main_apiset, main_apiset);
	if (rc < 0) {
		ERROR("Can't create supervision's apiset: %m");
		goto error;
	}

	/* export the service if required */
	if (main_config->ws_server) {
		rc = afb_api_ws_add_server(main_config->ws_server, main_apiset, main_apiset);
		if (rc < 0) {
			ERROR("Can't export (ws-server) api %s: %m", main_config->ws_server);
			goto error;
		}
	}

	/* start the services */
	if (afb_apiset_start_all_services(main_apiset) < 0)
		goto error;

#if WITH_LIBMICROHTTPD
	/* start the HTTP server */
	if (main_config->httpdPort <= 0) {
		ERROR("no port is defined");
		goto error;
	}

	if (!afb_hreq_init_cookie(main_config->httpdPort, main_config->rootapi, main_config->cntxTimeout)) {
		ERROR("initialisation of HTTP cookies failed");
		goto error;
	}

	hsrv = start_http_server();
	if (hsrv == NULL)
		goto error;
#endif

	/* ready */
#if WITH_SYSTEMD
	sd_notify(1, "READY=1");
#endif

	/* activate the watchdog */
#if HAS_WATCHDOG
	if (afb_watchdog_activate() < 0)
		ERROR("can't start the watchdog");
#endif

	/* discover binders */
	afs_supervisor_discover();
	return;
error:
	exit(1);
}

/**
 * initialize the supervision
 */
int main(int ac, char **av)
{
	/* scan arguments */
	main_config = optargs_parse(ac, av);
	if (main_config->name) {
		verbose_set_name(main_config->name, 0);
		process_name_set_name(main_config->name);
		process_name_replace_cmdline(av, main_config->name);
	}
	/* enter job processing */
	afb_sched_start(3, 0, 10, start, av[1]);
	WARNING("hoops returned from jobs_enter! [report bug]");
	return 1;
}

