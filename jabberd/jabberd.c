/* --------------------------------------------------------------------------
 *
 * License
 *
 * The contents of this file are subject to the Jabber Open Source License
 * Version 1.0 (the "License").  You may not copy or use this file, in either
 * source code or executable form, except in compliance with the License.  You
 * may obtain a copy of the License at http://www.jabber.com/license/ or at
 * http://www.opensource.org/.
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied.  See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * Copyrights
 *
 * Portions created by or assigned to Jabber.com, Inc. are
 * Copyright (c) 1999-2000 Jabber.com, Inc.  All Rights Reserved.  Contact
 * information for Jabber.com, Inc. is available at http://www.jabber.com/.
 *
 * Portions Copyright (c) 1998-1999 Jeremie Miller.
 *
 * Acknowledgements
 *
 * Special thanks to the Jabber Open Source Contributors for their
 * suggestions and support of Jabber.
 *
 * --------------------------------------------------------------------------*/

/*
 * Doesn't he wish!
 *
 * <ChatBot> jer: Do you sometimes wish you were written in perl?
 *
 */

#include "jabberd.h"

HASHTABLE cmd__line;

extern jabber_deliver j__deliver;

extern xmlnode greymatter__;

pool jabberd__runtime = NULL;

volatile int jab_shutdown = 0;

char wpserver_start[50];

/*** internal functions ***/
int configurate(char *file);
void static_init(void);
void dynamic_init(void);
void deliver_init(void);
void heartbeat_birth(void);
void heartbeat_death(void);
int configo(int exec);
void shutdown_callbacks(void);
int config_reload(char *file);
int instance_startup(xmlnode x, int exec);
void instance_shutdown(instance i);

void signal_11(int sig)
{
	signal(SIGSEGV, SIG_DFL);
}

void signal_2(int sig)
{
	jab_shutdown = 1;
}

void set_server_start()
{
	struct tm today;
	time_t ltime;

	/* server start time */
	time(&ltime);
	localtime_r(&ltime, &today);
	strftime(wpserver_start, 50, "%Y%m%d%H%M%S", &today);
}


void daemonize(void)
{
	pid_t pid;
	pid_t sid;
	int fd;

	log_debug("Daemonizing...");
	pid = fork();
	if (pid == -1)
		log_alert("error", "Failed to fork()");

	if (pid) {
		log_debug("Daemon born, pid %d.", pid);
		exit(0);
	}

	fd = open("/dev/null", O_RDWR);
	if (fd) {
		if (fd != 0)
			dup2(fd, 0);
		if (fd != 1)
			dup2(fd, 1);
		if (fd != 2)
			dup2(fd, 2);
		if (fd > 2)
			close(fd);
	}

	sid = setsid();
	if (sid == -1)
		abort();
	return;
}

int main(int argc, char **argv)
{
	int help, i;		/* temporary variables */
	char *cfgfile = NULL;
	char *c, *cmd, *home = NULL;	/* strings used to load the server config */
	char *user = NULL;
	uid_t uid = 0, euid = 0, newgid = 0;
	struct passwd *pwd;
	pool cfg_pool = pool_new();
	xmlnode pidfile;
	char *pidpath;

	jabberd__runtime = pool_new();

	/* start by assuming the parameters were entered correctly */
	help = 0;
	cmd__line =
	    ghash_create_pool(jabberd__runtime, 11,
			      (KEYHASHFUNC) str_hash_code,
			      (KEYCOMPAREFUNC) j_strcmp);

	/* process the parameterss one at a time */
	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-') {	/* make sure it's a valid command */
			help = 1;
			break;
		}
		for (c = argv[i] + 1; c[0] != '\0'; c++) {
			/* loop through the characters, like -Dc */
			if (*c == 'd') {
				daemonize();
				continue;
			}
			if (*c == 'D') {
				debug_flag = 1;
				continue;
			}
			if (*c == 'V' || *c == 'v') {
				printf("Jabberd Version %s\n", VERSION);
				exit(0);
			}

			cmd = pmalloco(cfg_pool, 2);
			cmd[0] = *c;
			if (i + 1 < argc) {
				ghash_put(cmd__line, cmd, argv[++i]);
			} else {
				help = 1;
				break;
			}
		}
	}

	/* were there any bad parameters? */
	if (help) {
		fprintf(stderr,
			"Usage:\njabberd &\n Optional Parameters:\n -c\t\t configuration file\n -D\t\tenable debug output\n -H\t\tlocation of home folder\n -v\t\tserver version\n -V\t\tserver version\n -d\t\trun as demon\n -u\t\tchange user and group\n");
		exit(0);
	}

	uid = getuid();
	euid = geteuid();

	user = ghash_get(cmd__line, "u");
	if (user) {
		if (uid != 0)
			log_alert(NULL, "Cannot change user.");
		else {
			pwd = getpwnam(user);
			if (!pwd)
				log_alert(NULL, "Couldn't find user %s",
					  user);
			if (newgid <= 0)
				newgid = pwd->pw_gid;
			if (setgid(newgid))
				log_alert(NULL, "Couldn't change group.");
			if (initgroups(user, newgid))
				log_alert(NULL, "Couldn't init groups.");
			if (setuid(pwd->pw_uid))
				log_alert(NULL, "Couldn't change user.");
		}
	}

	if ((home = ghash_get(cmd__line, "H")) == NULL)
		home = pstrdup(jabberd__runtime, HOME);

	/* change the current working directory so everything is "local" */
	if (home != NULL && chdir(home))
		fprintf(stderr, "Unable to access home folder %s: %s\n",
			home, strerror(errno));

	/* load the config passing the file if it was manually set */
	cfgfile = ghash_get(cmd__line, "c");
	if (configurate(cfgfile))
		exit(1);

	//ignore
	signal(1, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);

	//handle
	signal(2, signal_2);
	signal(3, signal_2);
	signal(15, signal_2);

	/* server start init */
	set_server_start();

	/* fire em up baby! */
	heartbeat_birth();

	/* init MIO */
	mio_init();

	static_init();
	dynamic_init();
	deliver_init();

	/* everything should be registered for the config pass, validate */
	j__deliver->deliver__flag = 0;	/* pause deliver() while starting up */
	if (configo(0))
		exit(1);

	/* karma granted, rock on */
	if (configo(1))
		exit(1);

	/* begin delivery of queued msgs */
	j__deliver->deliver__flag = 1;
	deliver(NULL, NULL);

	/* start */
	log_alert(NULL, "Jabberd started.");

	while (jab_shutdown != 1) {
		Sleep(1000);
	}

	log_alert(NULL, "Recieved Kill.  Jabberd shutting down.");

	//    log_debug("Stop instances");
	//    instance_shutdown(NULL); //???

	log_debug("Stop callbacks");

	/* Inform other servers in system, that we are going down */
	shutdown_callbacks();

	/* wait 1 second */
	sleep(1);

	/* stop heartbeat */
	heartbeat_death();

	/* wait until all sockets are ready to be closed ( accept ) */
	/* empty sockets buffers */
	log_debug("wait mio clean up buffers");
	mio_clean();

	/* empty queues */
	log_debug("MTQ stop");
	mtq_stop();

	j__deliver->deliver__flag = 0;

	//    log_debug("Stop instances");
	//    instance_shutdown(NULL);

	/* stop sockets */
	log_debug("MIO stop");
	mio_stop();

	/* Get rid of our pid file */
	pidfile = xmlnode_get_tag(greymatter__, "pidfile");
	if (pidfile != NULL) {
		pidpath = xmlnode_get_data(pidfile);
		if (pidpath != NULL)
			unlink(pidpath);
	}
	pool_free(cfg_pool);
	xmlnode_free(greymatter__);

	/* base modules use jabberd__runtime to know when to shutdown */
	pool_free(jabberd__runtime);

	/* we're done! */
	return 0;
}

DWORD timeGetTime()
{
	struct timeb tb;
	DWORD ret;
	ftime(&tb);
	ret = tb.time * 1000 + tb.millitm;
	return ret;
}

void Sleep(DWORD time)
{
	usleep(time * 1000);
}
