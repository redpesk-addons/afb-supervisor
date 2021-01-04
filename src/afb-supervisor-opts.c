/*
 * Copyright (C) 2015-2021 IoT.bzh Company
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
#include <string.h>
#include <getopt.h>
#include <limits.h>
#include <unistd.h>

#include <libafb/sys/verbose.h>
#include "afb-supervisor-opts.h"

#if !defined(AFB_SUPERVISOR_VERSION)
#  error "you should define AFB_SUPERVISOR_VERSION"
#endif

#define _STRINGIFY_(x) #x
#define STRINGIFY(x) _STRINGIFY_(x)

// default
#define DEFLT_CNTX_TIMEOUT  32000000	// default Client Connection
					// Timeout: few more than one year
#define DEFLT_API_TIMEOUT   20		// default Plugin API Timeout [0=NoLimit
					// for Debug Only]
#define DEFLT_CACHE_TIMEOUT 100000	// default Static File Chache
					// [Client Side Cache
					// 100000~=1day]
#define CTX_NBCLIENTS       10		// allow a default of 10 authenticated
					// clients


// Define command line option
#define SET_ROOT_DIR       6
#define SET_ROOT_BASE      7
#define SET_ROOT_API       8

#define SET_CACHE_TIMEOUT  10
#define SET_SESSION_DIR    11

#define SET_APITIMEOUT     14
#define SET_CNTXTIMEOUT    15

#define SET_SESSIONMAX     23

#define SET_ROOT_HTTP      26

#define DISPLAY_HELP       'h'
#define SET_NAME           'n'
#define SET_TCP_PORT       'p'
#define SET_QUIET          'q'
#define WS_SERVICE         's'
#define SET_UPLOAD_DIR     'u'
#define DISPLAY_VERSION    'V'
#define SET_VERBOSE        'v'
#define SET_WORK_DIR       'w'

const char shortopts[] =
	"hn:p:qrs:t:u:Vvw:"
;

// Command line structure hold cli --command + help text
typedef struct {
	int val;		// command number within application
	int has_arg;		// command number within application
	char *name;		// command as used in --xxxx cli
	char *help;		// help text
} AFB_options;

// Supported option
static AFB_options cliOptions[] = {
/* *INDENT-OFF* */
	{SET_VERBOSE,       0, "verbose",     "Verbose Mode, repeat to increase verbosity"},
	{SET_QUIET,         0, "quiet",       "Quiet Mode, repeat to decrease verbosity"},

	{SET_NAME,          1, "name",        "Set the visible name"},

	{SET_TCP_PORT,      1, "port",        "HTTP listening TCP port  [default " STRINGIFY(AFB_SUPERVISOR_PORT) "]"},
	{SET_ROOT_HTTP,     1, "roothttp",    "HTTP Root Directory [default no root http (files not served but apis still available)]"},
	{SET_ROOT_BASE,     1, "rootbase",    "Angular Base Root URL [default /opa]"},
	{SET_ROOT_API,      1, "rootapi",     "HTML Root API URL [default /api]"},

	{SET_APITIMEOUT,    1, "apitimeout",  "Binding API timeout in seconds [default 10]"},
	{SET_CNTXTIMEOUT,   1, "cntxtimeout", "Client Session Context Timeout [default 900]"},
	{SET_CACHE_TIMEOUT, 1, "cache-eol",   "Client cache end of live [default 3600]"},

	{SET_WORK_DIR,      1, "workdir",     "Set the working directory [default: $PWD or current working directory]"},
	{SET_UPLOAD_DIR,    1, "uploaddir",   "Directory for uploading files [default: workdir]"},
	{SET_ROOT_DIR,      1, "rootdir",     "Root Directory of the application [default: workdir]"},
	{SET_SESSION_DIR,   1, "sessiondir",  "OBSOLETE (was: Sessions file path)"},

	{WS_SERVICE,        1, "ws-server",   "Provide supervisor as websocket"},
	{DISPLAY_VERSION,   0, "version",     "Display version and copyright"},
	{DISPLAY_HELP,      0, "help",        "Display this help"},

	{SET_SESSIONMAX,    1, "session-max", "Max count of session simultaneously [default 10]"},

	{0, 0, NULL, NULL}
/* *INDENT-ON* */
};


struct enumdesc
{
	const char *name;
	int value;
};

/*----------------------------------------------------------
 | printversion
 |   print version and copyright
 +--------------------------------------------------------- */
static void printVersion(FILE * file)
{
	static const char version[] =
		"\n"
		"  afb-supervisor [Application Framework Supervisor] version="AFB_SUPERVISOR_VERSION"\n"
		"\n"
		"  Copyright (C) 2015-2021 IoT.bzh Company\n"
		"  afb-supervisor comes with ABSOLUTELY NO WARRANTY.\n"
		"  Licence Apache 2\n"
		"\n";

	fprintf(file, "%s", version);
}

/*----------------------------------------------------------
 | printHelp
 |   print information from long option array
 +--------------------------------------------------------- */

static void printHelp(FILE * file, const char *name)
{
	int ind;
	char command[50];

	fprintf(file, "%s:\nallowed options\n", name);
	for (ind = 0; cliOptions[ind].name != NULL; ind++) {
		strcpy(command, cliOptions[ind].name);
		if (cliOptions[ind].has_arg)
			strcat(command, "=xxxx");
		fprintf(file, "  --%-15s %s\n", command, cliOptions[ind].help);
	}
	fprintf(file,
		"Example:\n  %s  --verbose --port=1234\n",
		name);
}


/*---------------------------------------------------------
 |   helpers for argument scanning
 +--------------------------------------------------------- */

static const char *name_of_option(int optc)
{
	AFB_options *o = cliOptions;
	while (o->name && o->val != optc)
		o++;
	return o->name ? : "<unknown-option-name>";
}

static const char *current_argument(int optc)
{
	if (optarg == 0) {
		ERROR("option [--%s] needs a value i.e. --%s=xxx",
		      name_of_option(optc), name_of_option(optc));
		exit(1);
	}
	return optarg;
}

static char *argvalstr(int optc)
{
	char *result = strdup(current_argument(optc));
	if (result == NULL) {
		ERROR("can't alloc memory");
		exit(1);
	}
	return result;
}

static int argvalint(int optc, int mini, int maxi, int base)
{
	const char *beg, *end;
	long int val;
	beg = current_argument(optc);
	val = strtol(beg, (char**)&end, base);
	if (*end || end == beg) {
		ERROR("option [--%s] requires a valid integer (found %s)",
			name_of_option(optc), beg);
		exit(1);
	}
	if (val < (long int)mini || val > (long int)maxi) {
		ERROR("option [--%s] value out of bounds (not %d<=%ld<=%d)",
			name_of_option(optc), mini, val, maxi);
		exit(1);
	}
	return (int)val;
}

static int argvalintdec(int optc, int mini, int maxi)
{
	return argvalint(optc, mini, maxi, 10);
}

static void noarg(int optc)
{
	if (optarg != 0) {
		ERROR("option [--%s] need no value (found %s)", name_of_option(optc), optarg);
		exit(1);
	}
}

/*---------------------------------------------------------
 |   Parse option and launch action
 +--------------------------------------------------------- */

static void parse_arguments(int argc, char **argv, struct optargs *config)
{
	char *programName = argv[0];
	int optc, ind;
	int nbcmd;
	struct option *gnuOptions;

	// ------------------ Process Command Line -----------------------

	// build GNU getopt info from cliOptions
	nbcmd = sizeof(cliOptions) / sizeof(AFB_options);
	gnuOptions = malloc(sizeof(*gnuOptions) * (unsigned)nbcmd);
	for (ind = 0; ind < nbcmd; ind++) {
		gnuOptions[ind].name = cliOptions[ind].name;
		gnuOptions[ind].has_arg = cliOptions[ind].has_arg;
		gnuOptions[ind].flag = 0;
		gnuOptions[ind].val = cliOptions[ind].val;
	}

	// get all options from command line
	while ((optc = getopt_long(argc, argv, shortopts, gnuOptions, NULL)) != EOF) {
		switch (optc) {
		case SET_VERBOSE:
			verbose_inc();
			break;

		case SET_QUIET:
			verbose_dec();
			break;

		case SET_TCP_PORT:
			config->httpdPort = argvalintdec(optc, 1024, 32767);
			break;

		case SET_APITIMEOUT:
			config->apiTimeout = argvalintdec(optc, 0, INT_MAX);
			break;

		case SET_CNTXTIMEOUT:
			config->cntxTimeout = argvalintdec(optc, 0, INT_MAX);
			break;

		case SET_ROOT_DIR:
			config->rootdir = argvalstr(optc);
			INFO("Forcing Rootdir=%s", config->rootdir);
			break;

		case SET_ROOT_HTTP:
			config->roothttp = argvalstr(optc);
			INFO("Forcing Root HTTP=%s", config->roothttp);
			break;

		case SET_ROOT_BASE:
			config->rootbase = argvalstr(optc);
			INFO("Forcing Rootbase=%s", config->rootbase);
			break;

		case SET_ROOT_API:
			config->rootapi = argvalstr(optc);
			INFO("Forcing Rootapi=%s", config->rootapi);
			break;

		case SET_UPLOAD_DIR:
			config->uploaddir = argvalstr(optc);
			break;

		case SET_WORK_DIR:
			config->workdir = argvalstr(optc);
			break;

		case SET_CACHE_TIMEOUT:
			config->cacheTimeout = argvalintdec(optc, 0, INT_MAX);
			break;

		case SET_SESSIONMAX:
			config->nbSessionMax = argvalintdec(optc, 1, INT_MAX);
			break;

		case SET_NAME:
			config->name = argvalstr(optc);
			break;

		case WS_SERVICE:
			config->ws_server = argvalstr(optc);
			break;

		case DISPLAY_VERSION:
			noarg(optc);
			printVersion(stdout);
			exit(0);

		case DISPLAY_HELP:
			printHelp(stdout, programName);
			exit(0);

		default:
			exit(1);
		}
	}
	free(gnuOptions);
}

static void fulfill_config(struct optargs *config)
{
	// default HTTP port
	if (config->httpdPort == 0)
		config->httpdPort = AFB_SUPERVISOR_PORT;

	// default binding API timeout
	if (config->apiTimeout == 0)
		config->apiTimeout = DEFLT_API_TIMEOUT;

	// cache timeout default one hour
	if (config->cacheTimeout == 0)
		config->cacheTimeout = DEFLT_CACHE_TIMEOUT;

	// cache timeout default one hour
	if (config->cntxTimeout == 0)
		config->cntxTimeout = DEFLT_CNTX_TIMEOUT;

	// max count of sessions
	if (config->nbSessionMax == 0)
		config->nbSessionMax = CTX_NBCLIENTS;

	/* set directories */
	if (config->workdir == NULL)
		config->workdir = ".";

	if (config->rootdir == NULL)
		config->rootdir = ".";

	if (config->uploaddir == NULL)
		config->uploaddir = ".";

	// if no Angular/HTML5 rootbase let's try '/' as default
	if (config->rootbase == NULL)
		config->rootbase = "/opa";

	if (config->rootapi == NULL)
		config->rootapi = "/api";
}

static void dump(struct optargs *config)
{
#define NN(x)   (x)?:""
#define P(...)  fprintf(stderr, __VA_ARGS__)
#define PF(x)   P("-- %15s: ", #x)
#define PE      P("\n")
#define S(x)	PF(x);P("%s",NN(config->x));PE;
#define D(x)	PF(x);P("%d",config->x);PE;

	P("---BEGIN-OF-CONFIG---\n");
	S(rootdir)
	S(roothttp)
	S(rootbase)
	S(rootapi)
	S(workdir)
	S(uploaddir)
	S(name)
	S(ws_server)

	D(httpdPort)
	D(cacheTimeout)
	D(apiTimeout)
	D(cntxTimeout)
	D(nbSessionMax)
	P("---END-OF-CONFIG---\n");

#undef V
#undef E
#undef L
#undef B
#undef D
#undef S
#undef PE
#undef PF
#undef P
#undef NN
}

static void parse_environment(struct optargs *config)
{
}

struct optargs *optargs_parse(int argc, char **argv)
{
	struct optargs *result;

	result = calloc(1, sizeof *result);

	parse_environment(result);
	parse_arguments(argc, argv, result);
	fulfill_config(result);
	if (verbose_wants(Log_Level_Info))
		dump(result);
	return result;
}

