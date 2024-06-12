/*
 * Copyright (C) 2015-2024 IoT.bzh Company
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

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/un.h>

#include <json-c/json.h>

#include <afb/afb-auth.h>
#include <afb/afb-session.h>

#include <libafb/core/afb-cred.h>
#include <libafb/core/afb-req-common.h>
#include <libafb/core/afb-api-common.h>
#include <libafb/core/afb-apiset.h>
#include <libafb/core/afb-data.h>
#include <libafb/core/afb-type.h>
#include <libafb/core/afb-evt.h>
#include <libafb/core/afb-json-legacy.h>
#include <libafb/wsapi/afb-stub-ws.h>

#include <libafb/sys/ev-mgr.h>
#include <libafb/core/afb-ev-mgr.h>
#include <libafb/misc/afb-socket.h>

#include <libafb/misc/afb-verbose.h>
#include <libafb/sys/x-socket.h>
#include <libafb/sys/x-mutex.h>
#include <libafb/sys/x-errno.h>

#include <libafb/misc/afb-supervisor.h>

#include "afb-supervisor-api.h"
#include "afb-discover.h"

/* supervised items */
struct supervised
{
	/* link to the next supervised */
	struct supervised *next;

	/* credentials of the supervised */
	struct afb_cred *cred;

	/* connection with the supervised */
	struct afb_stub_ws *stub;

	/* pid */
	int pid;
};

/* api and apiset name */
static const char supervision_apiname[] = AFB_SUPERVISION_APINAME;
static const char supervisor_apiname[] = AFB_SUPERVISOR_APINAME;

/* the empty apiset */
static struct afb_apiset *empty_apiset;

/* supervision socket path */
static const char supervision_socket_path[] = "unix:" AFB_SUPERVISOR_SOCKET;
static struct ev_fd *supervision_efd;

/* global mutex */
static x_mutex_t mutex = X_MUTEX_INITIALIZER;

/* list of supervised daemons */
static struct supervised *superviseds;

/* events */
static struct afb_evt *event_add_pid;
static struct afb_evt *event_del_pid;

/*************************************************************************************/


/*************************************************************************************/

/**
 * send on 'fd' an initiator with 'command'
 * return 0 on success or -1 on failure
 */
static int send_initiator(int fd, const char *command)
{
	int rc;
	ssize_t swr;
	struct afb_supervisor_initiator asi;

	/* set  */
	memset(&asi, 0, sizeof asi);
	strcpy(asi.interface, AFB_SUPERVISOR_INTERFACE_1);
	if (command)
		strncpy(asi.extra, command, sizeof asi.extra - 1);

	/* send the initiator */
	swr = write(fd, &asi, sizeof asi);
	if (swr < 0) {
		rc = -errno;
		LIBAFB_ERROR("Can't send initiator: %s", strerror(-rc));
	} else if (swr < sizeof asi) {
		LIBAFB_ERROR("Sending incomplete initiator!");
		rc = X_EAGAIN;
	} else
		rc = 0;
	return rc;
}

#if WITH_CRED
/*
 * checks whether the incoming supervised represented by its creds
 * is to be accepted or not.
 * return 1 if yes or 0 otherwise.
 */
static int should_accept(struct afb_cred *cred)
{
	return cred && cred->pid != getpid(); /* not me! */
}
#endif

static void on_supervised_hangup(struct afb_stub_ws *stub)
{
	struct supervised *s, **ps;

	/* Search the supervised of the ws-stub */
	x_mutex_lock(&mutex);
	ps = &superviseds;
	while ((s = *ps) && s->stub != stub)
		ps = &s->next;

	/* unlink the supervised if found */
	if (s)
		*ps = s->next;
	x_mutex_unlock(&mutex);

	/* forgive the ws-stub */
	afb_stub_ws_unref(stub);

	/* forgive the supervised */
	if (s) {
		afb_json_legacy_event_push(event_del_pid, json_object_new_int((int)s->pid));
#if WITH_CRED
		afb_cred_unref(s->cred);
#endif
		free(s);
	}
}

/*
 * create a supervised for socket 'fd' and 'cred'
 * return the pid > 0 in case of success or -1 in case of error
 */
#if WITH_CRED
static int make_supervised(int fd, struct afb_cred *cred)
#else
static int make_supervised(int fd)
#endif
{
	struct supervised *s;

	s = malloc(sizeof *s);
	if (!s)
		return X_ENOMEM;

	s->stub = afb_stub_ws_create_client(fd, 1, supervision_apiname, empty_apiset);
	if (!s->stub) {
		free(s);
		return -1;
	}
	x_mutex_lock(&mutex);
#if WITH_CRED
	s->cred = cred;
	s->pid = (int)cred->pid;
#else
	{
		static int x = 0;

		struct supervised *i;
		do {
			if (++x < 0)
				x = 1;
			i = superviseds;
			while (i && i->pid != x)
				i = i->next;
		} while (i);
		s->pid = x;
	}
#endif
	s->next = superviseds;
	superviseds = s;
	x_mutex_unlock(&mutex);
	afb_stub_ws_set_on_hangup(s->stub, on_supervised_hangup);
	return s->pid;
}

/**
 * Search the supervised of 'pid', return it or NULL.
 */
static struct supervised *supervised_of_pid(int pid)
{
	struct supervised *s;

	x_mutex_lock(&mutex);
	s = superviseds;
	while (s && pid != s->pid)
		s = s->next;
	x_mutex_unlock(&mutex);

	return s;
}

/*
 * handles incoming connection on 'sock'
 */
static void accept_supervision_link(int sock)
{
	int rc, fd;
	struct sockaddr addr;
	socklen_t lenaddr;
#if WITH_CRED
	struct afb_cred *cred;
#endif

	lenaddr = (socklen_t)sizeof addr;
	fd = accept(sock, &addr, &lenaddr);
	if (fd >= 0) {
#if WITH_CRED
		afb_cred_create_for_socket(&cred, fd);
		rc = should_accept(cred);
		if (rc) {
#endif
			rc = send_initiator(fd, NULL);
			if (!rc) {
#if WITH_CRED
				rc = make_supervised(fd, cred);
#else
				rc = make_supervised(fd);
#endif
				if (rc > 0) {
					afb_json_legacy_event_push(event_add_pid, json_object_new_int(rc));
					return;
				}
			}
#if WITH_CRED
		}
		afb_cred_unref(cred);
#endif
		close(fd);
	}
}

/*
 * handle even on server socket
 */
static void listening(struct ev_fd *efd, int fd, uint32_t revents, void *closure)
{
	if ((revents & EPOLLHUP) != 0) {
		LIBAFB_ERROR("supervision socket closed");
		exit(1);
	}
	if ((revents & EPOLLIN) != 0)
		accept_supervision_link(fd);
}

/*
 */
static void discovered_cb(void *closure, pid_t pid)
{
	struct supervised *s;

	s = supervised_of_pid(pid);
	if (!s) {
		(*(int*)closure)++;
		kill(pid, SIGHUP);
	}
}

int afs_supervisor_discover()
{
	int n = 0;
	afs_discover("afb-daemon", discovered_cb, &n);
	return n;
}

/*************************************************************************************/

static void f_subscribe(struct afb_req_common *req, struct json_object *args)
{
	int revoke, ok;

	revoke = json_object_is_type(args, json_type_boolean)
		&& !json_object_get_boolean(args);

	ok = 1;
	if (!revoke) {
		ok = !afb_req_common_subscribe(req, event_add_pid)
			&& !afb_req_common_subscribe(req, event_del_pid);
	}
	if (revoke || !ok) {
		afb_req_common_unsubscribe(req, event_add_pid);
		afb_req_common_unsubscribe(req, event_del_pid);
	}
	afb_json_legacy_req_reply_hookable(req, NULL, ok ? NULL : "error", NULL);
}

static void f_list(struct afb_req_common *req, struct json_object *args)
{
	char pid[50];
	struct json_object *resu, *item;
	struct supervised *s;

	resu = json_object_new_object();
	s = superviseds;
	while (s) {
		sprintf(pid, "%d", (int)s->pid);
		item = NULL;
#if WITH_CRED
		resu = json_object_new_object();
		json_object_object_add(resu, "pid", json_object_new_int((int)s->cred->pid));
		json_object_object_add(resu, "uid", json_object_new_int((int)s->cred->uid));
		json_object_object_add(resu, "gid", json_object_new_int((int)s->cred->gid));
		json_object_object_add(resu, "id", json_object_new_string(s->cred->id));
		json_object_object_add(resu, "label", json_object_new_string(s->cred->label));
		json_object_object_add(resu, "user", json_object_new_string(s->cred->user));
#endif
		json_object_object_add(resu, pid, item);
		s = s->next;
	}
	afb_json_legacy_req_reply_hookable(req, resu, NULL, NULL);
}

static void f_discover(struct afb_req_common *req, struct json_object *args)
{
	afs_supervisor_discover();
	afb_json_legacy_req_reply_hookable(req, NULL, NULL, NULL);
}

static void propagate(struct afb_req_common *req, struct json_object *args, const char *verb)
{
	struct json_object *item;
	struct supervised *s;
	struct afb_api_item api;
	struct afb_data *data;
	int p, rc;

	/* extract the pid */
	if (!json_object_object_get_ex(args, "pid", &item)) {
		afb_json_legacy_req_reply_hookable(req, NULL, "no-pid", NULL);
		return;
	}

	p = json_object_get_int(item);
	if (!p) {
		afb_json_legacy_req_reply_hookable(req, NULL, "bad-pid", NULL);
		return;
	}

	/* get supervised of pid */
	s = supervised_of_pid((pid_t)p);
	if (!s) {
		afb_json_legacy_req_reply_hookable(req, NULL, "unknown-pid", NULL);
		return;
	}
	json_object_object_del(args, "pid");

	rc = afb_json_legacy_make_data_json_c(&data, json_object_get(args));
	if (rc < 0) {
		afb_json_legacy_req_reply_hookable(req, NULL, "internal-error", NULL);
		return;
	}

	/* forward it now */
	afb_req_common_prepare_forwarding(req, "S", verb, 1, &data);
	api = afb_stub_ws_client_api(s->stub);
	api.itf->process(api.closure, req);
}

static void f_do(struct afb_req_common *req, struct json_object *args)
{
	propagate(req, args, NULL);
}

static void f_config(struct afb_req_common *req, struct json_object *args)
{
	propagate(req, args, NULL);
}

static void f_trace(struct afb_req_common *req, struct json_object *args)
{
	propagate(req, args, NULL);
}

static void f_sessions(struct afb_req_common *req, struct json_object *args)
{
	propagate(req, args, "slist");
}

static void f_session_close(struct afb_req_common *req, struct json_object *args)
{
	propagate(req, args, "sclose");
}

static void f_exit(struct afb_req_common *req, struct json_object *args)
{
	propagate(req, args, NULL);
	afb_json_legacy_req_reply_hookable(req, NULL, NULL, NULL);
}

static void f_debug_wait(struct afb_req_common *req, struct json_object *args)
{
	propagate(req, args, "wait");
	afb_json_legacy_req_reply_hookable(req, NULL, NULL, NULL);
}

static void f_debug_break(struct afb_req_common *req, struct json_object *args)
{
	propagate(req, args, "break");
	afb_json_legacy_req_reply_hookable(req, NULL, NULL, NULL);
}

/***************************************************************************/

static struct afb_api_common *supervisor_api;

static void supervisor_process(void *closure, struct afb_req_common *req);
static void supervisor_describe(void *closure, void (*describecb)(void *, struct json_object *), void *clocb);

static struct afb_api_itf supervisor_itf =
{
	.process = supervisor_process,
	.describe = supervisor_describe
};

static const struct afb_auth _afb_auths_v2_supervisor[] = {
	/* 0 */
	{
		.type = afb_auth_Permission,
		.text = "urn:AGL:permission:#supervision:platform:access"
	}
};

/***************************************************************************/

void checkcb(void *closure, int status)
{
	struct afb_req_common *req = closure;
	void (*fun)(struct afb_req_common*, struct json_object*);

	if (status <= 0)
		return;

	fun = NULL;
	switch (req->verbname[0]) {
	case 'c':
		if (!strcmp(req->verbname, "config"))
			fun = f_config;
		break;

	case 'd':
		if (!strcmp(req->verbname, "do"))
			fun = f_do;
		else if (!strcmp(req->verbname, "discover"))
			fun = f_discover;
		else if (!strcmp(req->verbname, "debug-wait"))
			fun = f_debug_wait;
		else if (!strcmp(req->verbname, "debug-break"))
			fun = f_debug_break;
		break;

	case 'e':
		if (!strcmp(req->verbname, "exit"))
			fun = f_exit;
		break;

	case 'l':
		if (!strcmp(req->verbname, "list"))
			fun = f_list;
		break;

	case 's':
		if (!strcmp(req->verbname, "subscribe"))
			fun = f_subscribe;
		else if (!strcmp(req->verbname, "sessions"))
			fun = f_sessions;
		else if (!strcmp(req->verbname, "session-close"))
			fun = f_session_close;
		break;

	case 't':
		if (!strcmp(req->verbname, "trace"))
			fun = f_trace;
		break;

	default:
		break;
	}
	if (fun == NULL) {
		afb_req_common_reply_verb_unknown_error_hookable(req);
	}
	else {
		afb_json_legacy_do_single_json_c(req->params.ndata, req->params.data, (void(*)(void*,struct json_object*))fun, req);
	}
}

static void supervisor_process(void *closure, struct afb_req_common *req)
{
	afb_req_common_check_and_set_session_async(req, &_afb_auths_v2_supervisor[0], AFB_SESSION_CHECK, checkcb, req);
}

static void supervisor_describe(void *closure, void (*describecb)(void *, struct json_object *), void *clocb)
{
	describecb(clocb, NULL /* TODO */);
}

int afs_supervisor_add(struct afb_apiset *declare_set, struct afb_apiset *call_set)
{
	struct afb_api_item item;
	int rc, fd;

	rc = 0;

	/* create api */
	if (rc == 0 && !supervisor_api) {
		supervisor_api = malloc(sizeof *supervisor_api);
		if (supervisor_api == NULL) {
			rc = X_ENOMEM;
		}
		else {
			afb_api_common_init(supervisor_api, declare_set, call_set, supervisor_apiname, 0, NULL, 0, NULL, 0, supervisor_api);
			supervisor_api->state = Api_State_Run;
			item.closure = NULL;
			item.group = NULL;
			item.itf = &supervisor_itf;
			rc = afb_apiset_add(declare_set, supervisor_apiname, item);
		}
	}

	/* create events */
	if (rc == 0 && !event_add_pid) {
		rc = afb_api_common_new_event(supervisor_api, "add-pid", &event_add_pid);
	}
	if (rc == 0 && !event_del_pid) {
		rc = afb_api_common_new_event(supervisor_api, "del-pid", &event_del_pid);
	}

	/* create an empty set for superviseds */
	if (rc == 0 && !empty_apiset) {
		empty_apiset = afb_apiset_create(supervision_apiname, 0);
		if (!empty_apiset) {
			LIBAFB_ERROR("Can't create supervision apiset");
			rc = X_ENOMEM;
		}
	}

	/* create supervision socket */
	if (rc == 0 && !supervision_efd) {
		rc = afb_socket_open(supervision_socket_path, 1);
		if (rc >= 0) {
			fd = rc;
			rc = afb_ev_mgr_add_fd(&supervision_efd, fd, EPOLLIN, listening, 0, 0, 1);
			if (rc < 0)
				close(fd);
		}
	}

	return rc;
}
