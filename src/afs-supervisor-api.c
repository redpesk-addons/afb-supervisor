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

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/un.h>

#include <json-c/json.h>

#define AFB_BINDING_VERSION 3
#define AFB_BINDING_NO_ROOT
#include <afb/afb-binding.h>

#include <libafb/core/afb-cred.h>
#include <libafb/core/afb-xreq.h>
#include <libafb/core/afb-api-v3.h>
#include <libafb/core/afb-apiset.h>
#include <libafb/wsapi/afb-stub-ws.h>
#include <libafb/misc/afb-fdev.h>
#include <libafb/misc/afb-socket-fdev.h>

#include <libafb/sys/fdev.h>
#include <libafb/sys/verbose.h>
#include <libafb/utils/wrap-json.h>
#include <libafb/sys/x-socket.h>
#include <libafb/sys/x-mutex.h>
#include <libafb/sys/x-errno.h>

#include <libafb/misc/afs-supervision.h>

#include "afs-supervisor-api.h"
#include "afs-discover.h"

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
static const char supervision_apiname[] = AFS_SUPERVISION_APINAME;
static const char supervisor_apiname[] = AFS_SUPERVISOR_APINAME;

/* the empty apiset */
static struct afb_apiset *empty_apiset;

/* supervision socket path */
static const char supervision_socket_path[] = AFS_SUPERVISION_SOCKET;
static struct fdev *supervision_fdev;

/* global mutex */
static x_mutex_t mutex = X_MUTEX_INITIALIZER;

/* list of supervised daemons */
static struct supervised *superviseds;

/* events */
static afb_event_t event_add_pid;
static afb_event_t event_del_pid;

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
	struct afs_supervision_initiator asi;

	/* set  */
	memset(&asi, 0, sizeof asi);
	strcpy(asi.interface, AFS_SUPERVISION_INTERFACE_1);
	if (command)
		strncpy(asi.extra, command, sizeof asi.extra - 1);

	/* send the initiator */
	swr = write(fd, &asi, sizeof asi);
	if (swr < 0) {
		rc = -errno;
		ERROR("Can't send initiator: %s", strerror(-rc));
	} else if (swr < sizeof asi) {
		ERROR("Sending incomplete initiator!");
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
		afb_event_push(event_del_pid, json_object_new_int((int)s->pid));
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
	struct fdev *fdev;

	s = malloc(sizeof *s);
	if (!s)
		return X_ENOMEM;

	fdev = afb_fdev_create(fd);
	if (!fdev) {
		free(s);
		return -1;
	}

	s->stub = afb_stub_ws_create_client(fdev, supervision_apiname, empty_apiset);
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
					afb_event_push(event_add_pid, json_object_new_int(rc));
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
static void listening(void *closure, uint32_t revents, struct fdev *fdev)
{
	if ((revents & EPOLLHUP) != 0) {
		ERROR("supervision socket closed");
		exit(1);
	}
	if ((revents & EPOLLIN) != 0)
		accept_supervision_link((int)(intptr_t)closure);
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

static void f_subscribe(afb_req_t req)
{
	struct json_object *args = afb_req_json(req);
	int revoke, ok;

	revoke = json_object_is_type(args, json_type_boolean)
		&& !json_object_get_boolean(args);

	ok = 1;
	if (!revoke) {
		ok = !afb_req_subscribe(req, event_add_pid)
			&& !afb_req_subscribe(req, event_del_pid);
	}
	if (revoke || !ok) {
		afb_req_unsubscribe(req, event_add_pid);
		afb_req_unsubscribe(req, event_del_pid);
	}
	afb_req_reply(req, NULL, ok ? NULL : "error", NULL);
}

static void f_list(afb_req_t req)
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
		wrap_json_pack(&item, "{si si si ss ss ss}",
				"pid", (int)s->cred->pid,
				"uid", (int)s->cred->uid,
				"gid", (int)s->cred->gid,
				"id", s->cred->id,
				"label", s->cred->label,
				"user", s->cred->user
				);
#endif
		json_object_object_add(resu, pid, item);
		s = s->next;
	}
	afb_req_success(req, resu, NULL);
}

static void f_discover(afb_req_t req)
{
	afs_supervisor_discover();
	afb_req_success(req, NULL, NULL);
}

static void propagate(afb_req_t req, const char *verb)
{
	struct afb_xreq *xreq;
	struct json_object *args, *item;
	struct supervised *s;
	struct afb_api_item api;
	int p;

	xreq = xreq_from_req_x2(req);
	args = afb_xreq_json(xreq);

	/* extract the pid */
	if (!json_object_object_get_ex(args, "pid", &item)) {
		afb_xreq_reply(xreq, NULL, "no-pid", NULL);
		return;
	}

	p = json_object_get_int(item);
	if (!p) {
		afb_xreq_reply(xreq, NULL, "bad-pid", NULL);
		return;
	}

	/* get supervised of pid */
	s = supervised_of_pid((pid_t)p);
	if (!s) {
		afb_req_reply(req, NULL, "unknown-pid", NULL);
		return;
	}
	json_object_object_del(args, "pid");

	/* replace the verb to call if needed */
	if (verb)
		xreq->request.called_verb = verb;

	/* call it now */
	api = afb_stub_ws_client_api(s->stub);
	api.itf->call(api.closure, xreq);
}

static void f_do(afb_req_t req)
{
	propagate(req, NULL);
}

static void f_config(afb_req_t req)
{
	propagate(req, NULL);
}

static void f_trace(afb_req_t req)
{
	propagate(req, NULL);
}

static void f_sessions(afb_req_t req)
{
	propagate(req, "slist");
}

static void f_session_close(afb_req_t req)
{
	propagate(req, "sclose");
}

static void f_exit(afb_req_t req)
{
	propagate(req, NULL);
	afb_req_success(req, NULL, NULL);
}

static void f_debug_wait(afb_req_t req)
{
	propagate(req, "wait");
	afb_req_success(req, NULL, NULL);
}

static void f_debug_break(afb_req_t req)
{
	propagate(req, "break");
	afb_req_success(req, NULL, NULL);
}

/*************************************************************************************/

/**
 * initialize the supervisor
 */
static int init_supervisor(afb_api_t api)
{
	event_add_pid = afb_api_make_event(api, "add-pid");
	if (!afb_event_is_valid(event_add_pid)) {
		ERROR("Can't create added event");
		return X_ENOMEM;
	}

	event_del_pid = afb_api_make_event(api, "del-pid");
	if (!afb_event_is_valid(event_del_pid)) {
		ERROR("Can't create deleted event");
		return X_ENOMEM;
	}

	/* create an empty set for superviseds */
	empty_apiset = afb_apiset_create(supervision_apiname, 0);
	if (!empty_apiset) {
		ERROR("Can't create supervision apiset");
		return X_ENOMEM;
	}

	/* create the supervision socket */
	supervision_fdev = afb_socket_fdev_open(supervision_socket_path, 1);
	if (!supervision_fdev)
		return -1;

	fdev_set_events(supervision_fdev, EPOLLIN);
	fdev_set_callback(supervision_fdev, listening,
			  (void*)(intptr_t)fdev_fd(supervision_fdev));

	return 0;
}

/*************************************************************************************/

static const struct afb_auth _afb_auths_v2_supervisor[] = {
	/* 0 */
	{
		.type = afb_auth_Permission,
		.text = "urn:AGL:permission:#supervision:platform:access"
	}
};

static const struct afb_verb_v3 _afb_verbs_supervisor[] = {
    {
        .verb = "subscribe",
        .callback = f_subscribe,
        .auth = &_afb_auths_v2_supervisor[0],
        .info = NULL,
        .session = AFB_SESSION_CHECK_X2
    },
    {
        .verb = "list",
        .callback = f_list,
        .auth = &_afb_auths_v2_supervisor[0],
        .info = NULL,
        .session = AFB_SESSION_CHECK_X2
    },
    {
        .verb = "config",
        .callback = f_config,
        .auth = &_afb_auths_v2_supervisor[0],
        .info = NULL,
        .session = AFB_SESSION_CHECK_X2
    },
    {
        .verb = "do",
        .callback = f_do,
        .auth = &_afb_auths_v2_supervisor[0],
        .info = NULL,
        .session = AFB_SESSION_CHECK_X2
    },
    {
        .verb = "trace",
        .callback = f_trace,
        .auth = &_afb_auths_v2_supervisor[0],
        .info = NULL,
        .session = AFB_SESSION_CHECK_X2
    },
    {
        .verb = "sessions",
        .callback = f_sessions,
        .auth = &_afb_auths_v2_supervisor[0],
        .info = NULL,
        .session = AFB_SESSION_CHECK_X2
    },
    {
        .verb = "session-close",
        .callback = f_session_close,
        .auth = &_afb_auths_v2_supervisor[0],
        .info = NULL,
        .session = AFB_SESSION_CHECK_X2
    },
    {
        .verb = "exit",
        .callback = f_exit,
        .auth = &_afb_auths_v2_supervisor[0],
        .info = NULL,
        .session = AFB_SESSION_CHECK_X2
    },
    {
        .verb = "debug-wait",
        .callback = f_debug_wait,
        .auth = &_afb_auths_v2_supervisor[0],
        .info = NULL,
        .session = AFB_SESSION_CHECK_X2
    },
    {
        .verb = "debug-break",
        .callback = f_debug_break,
        .auth = &_afb_auths_v2_supervisor[0],
        .info = NULL,
        .session = AFB_SESSION_CHECK_X2
    },
    {
        .verb = "discover",
        .callback = f_discover,
        .auth = &_afb_auths_v2_supervisor[0],
        .info = NULL,
        .session = AFB_SESSION_CHECK_X2
    },
    { .verb = NULL }
};

static const struct afb_binding_v3 _afb_binding_supervisor = {
    .api = supervisor_apiname,
    .specification = NULL,
    .info = NULL,
    .verbs = _afb_verbs_supervisor,
    .preinit = NULL,
    .init = init_supervisor,
    .onevent = NULL,
    .noconcurrency = 0
};

int afs_supervisor_add(
		struct afb_apiset *declare_set,
		struct afb_apiset * call_set)
{
	return -!afb_api_v3_from_binding(&_afb_binding_supervisor, declare_set, call_set);
}

