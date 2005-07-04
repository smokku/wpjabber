/* --------------------------------------------------------------------------
 *
 *  WP
 *
 * -------------------------------------------------------------------------- */

#include "wpj.h"

extern int KARMA_DEF_INIT;
extern int KARMA_DEF_MAX;
extern int KARMA_DEF_INC;
extern int KARMA_DEF_DEC;
extern int KARMA_DEF_PENALTY;
extern int KARMA_DEF_RESTORE;
extern int DEF_RATE_TIME;
extern int DEF_RATE_CONN;
extern int DEF_CONN;

/* main structure pointer */
wpjs wpj;
volatile int wpj_shutdown = 0;

void signal_end(int sig)
{
	wpj_shutdown = 1;
}

void update_links_count()
{
	FILE *plik;

	plik = fopen(wpj->cfg->statfile, "w");
	if (!plik)
		plik = fopen("/tmp/wpj_stats_file", "w");
	if (plik) {
#ifdef HAVE_SSL

		if (wpj->cfg->ssl == 1) {
			fprintf(plik, "%d\n%d", wpj->clients_count,
				wpj->clients_ssl);
		} else {
			fprintf(plik, "%d", wpj->clients_count);
		}
#else
		fprintf(plik, "%d", wpj->clients_count);
#endif

		fclose(plik);
	}
}

/* connect and alloc */
void connect_serwer()
{
	server_info si;
	wp_connection conn;

	wpj->cfg->si = NULL;
	conn = wpj->cfg->connection;

	si = pmalloco(wpj->p, sizeof(_server_info));
	si->secret = conn->secret;
	si->mhost = conn->mhost;
	si->mport = conn->mport;
	si->mip = conn->mip;
	si->state = conn_CLOSED;

	wpj->si = si;
	wpj->cfg->si = si;

	/* connect */
	io_connect(si->mip, si->mport, server_process_xml, (void *) si, 0);
}

/* Allow configuration file */
int configure(char *cfgfile)
{
	xmlnode xcfg, tmp;

	xcfg = xmlnode_file(cfgfile);
	if (!xcfg) {
		log_alert("Error", "Unable to parse config file: %s",
			  cfgfile);
		return 0;
	}

	/* Our pidfile */
	tmp = xmlnode_get_tag(xcfg, "pidfile");
	if (tmp != NULL) {
		wpj->cfg->pidfile =
		    pstrdup(wpj->p, xmlnode_get_tag_data(xcfg, "pidfile"));
	} else
		wpj->cfg->pidfile = "pidfile";

	/* stats file */
	tmp = xmlnode_get_tag(xcfg, "statfile");
	if (tmp != NULL) {
		wpj->cfg->statfile =
		    pstrdup(wpj->p,
			    xmlnode_get_tag_data(xcfg, "statfile"));
	} else
		wpj->cfg->statfile = "conn_number";

	/* log file */
	tmp = xmlnode_get_tag(xcfg, "logfile");
	if (tmp != NULL) {
		wpj->cfg->logfile =
		    pstrdup(wpj->p, xmlnode_get_tag_data(xcfg, "logfile"));
	} else
		wpj->cfg->logfile = "log_";

	/* init logs */
	init_log(wpj->cfg->logfile);

	log_alert("wpj", "processing configuration ...........");

	/* our hostname */
	wpj->cfg->hostname =
	    pstrdup(wpj->p, xmlnode_get_tag_data(xcfg, "hostname"));
	if (!wpj->cfg->hostname) {
		log_alert("config",
			  "Required hostname not supplied in config.");
		return 0;
	}

	wpj->cfg->httpforward =
	    pstrdup(wpj->p, xmlnode_get_tag_data(xcfg, "httpforward"));
	if (wpj->cfg->httpforward) {
		log_alert("config",
			  "HTTP connections will be forwarded to %s",
			  wpj->cfg->httpforward);
	}

	/* ping time */
	wpj->cfg->ping_time =
	    j_atoi(xmlnode_get_tag_data(xcfg, "pingtime"), DEF_PINGTIME);

	/* ping check  */
	wpj->cfg->iocheck =
	    j_atoi(xmlnode_get_tag_data(xcfg, "pingcheck"), DEF_IOCHECK);

	/* ioclose */
	wpj->cfg->ioclose =
	    j_atoi(xmlnode_get_tag_data(xcfg, "pingclose"), DEF_IOCLOSE);

	log_alert("config",
		  "check ping %d ,send ping every %d, close after no pong after %d",
		  wpj->cfg->iocheck, wpj->cfg->ping_time,
		  wpj->cfg->ioclose);

	/* close after when no auth */
	wpj->cfg->auth_time =
	    j_atoi(xmlnode_get_tag_data(xcfg, "authtime"), DEF_AUTHTIME);

	/* check sockets out of karma */
	wpj->cfg->datacheck =
	    j_atoi(xmlnode_get_tag_data(xcfg, "datacheck"), DEF_DATACHECK);

	/* time to wait with reconnect */
	wpj->cfg->reconnect_time =
	    j_atoi(xmlnode_get_tag_data(xcfg, "reconnecttime"),
		   DEF_RECONNECTTIME);


	/* Setup our listene info */
	tmp = xmlnode_get_tag(xcfg, "listen");
	wpj->cfg->lport = j_atoi(xmlnode_get_tag_data(tmp, "port"), 5222);
	wpj->cfg->lip = pstrdup(wpj->p, xmlnode_get_tag_data(tmp, "ip"));
	if (!wpj->cfg->lip) {
		log_alert("config", "WPJ will listen on all interfaces");
	}

	/* auto refresh sessions */
	wpj->cfg->auto_refresh_sessions =
	    xmlnode_get_tag(xcfg, "auto_refresh_sessions") ? 1 : 0;

	if (wpj->cfg->auto_refresh_sessions) {
		log_alert("config",
			  "recover session after server restart enabled");
	}

	wpj->cfg->no_message_storage =
	    xmlnode_get_tag(xcfg, "message_recovery") ? 0 : 1;

	if (wpj->cfg->no_message_storage) {
		log_alert("config",
			  "user messages storage and recovery disabled");
	} else {
		log_alert("config",
			  "user messages storage and recovery enabled");
	}

	tmp = xmlnode_get_tag(xcfg, "connect");

	if (tmp) {
		wp_connection conn;

		conn = pmalloco(wpj->p, sizeof(_wp_connection));
		conn->mhost =
		    pstrdup(wpj->p, xmlnode_get_tag_data(tmp, "host"));
		conn->mip =
		    pstrdup(wpj->p, xmlnode_get_tag_data(tmp, "ip"));
		conn->secret =
		    pstrdup(wpj->p, xmlnode_get_tag_data(tmp, "secret"));
		conn->mport =
		    j_atoi(xmlnode_get_tag_data(tmp, "port"), 5225);

		if (!conn->mhost || !conn->secret || !conn->mip) {
			log_alert("config",
				  "Server hostname or secret or ip were not specified.");
			return 0;
		}

		log_alert("config", "connect host: %s on %s:%d",
			  conn->mhost, conn->mip, conn->mport);

		wpj->cfg->connection = conn;
	} else {
		log_alert("config", "Didn't find connect");
		return 0;
	}

	/* karma config */
	tmp = xmlnode_get_tag(xcfg, "karma");
	if (tmp != NULL) {
		KARMA_DEF_INIT =
		    j_atoi(xmlnode_get_tag_data(tmp, "init"), KARMA_INIT);
		KARMA_DEF_MAX =
		    j_atoi(xmlnode_get_tag_data(tmp, "max"), KARMA_MAX);
		KARMA_DEF_INC =
		    j_atoi(xmlnode_get_tag_data(tmp, "inc"), KARMA_INC);
		KARMA_DEF_DEC =
		    j_atoi(xmlnode_get_tag_data(tmp, "dec"), KARMA_DEC);
		KARMA_DEF_PENALTY =
		    j_atoi(xmlnode_get_tag_data(tmp, "penalty"),
			   KARMA_PENALTY);
		KARMA_DEF_RESTORE =
		    j_atoi(xmlnode_get_tag_data(tmp, "restore"),
			   KARMA_RESTORE);

		log_alert("config", "KARMA_DEF_INIT %d", KARMA_DEF_INIT);
		log_alert("config", "KARMA_DEF_MAX %d", KARMA_DEF_MAX);
		log_alert("config", "KARMA_DEF_INC %d", KARMA_DEF_INC);
		log_alert("config", "KARMA_DEF_DEC %d", KARMA_DEF_DEC);
		log_alert("config", "KARMA_DEF_PENALTY %d",
			  KARMA_DEF_PENALTY);
		log_alert("config", "KARMA_DEF_RESTORE %d",
			  KARMA_DEF_RESTORE);

		wpj->cfg->karma_time =
		    j_atoi(xmlnode_get_tag_data(tmp, "heartbeat"), 2);
		log_alert("config", "cfg->karma_time  %d",
			  wpj->cfg->karma_time);
	}
#ifdef HAVE_SSL
	wpj->cfg->ssl = 0;
	tmp = xmlnode_get_tag(xcfg, "ssl");
	if (tmp != NULL) {
		if (io_ssl_init(tmp) == 0)
			wpj->cfg->ssl = 1;
	}

	if (wpj->cfg->ssl) {
		log_alert("config", "SSL on");
	} else {
		log_alert("config", "No SSL");
	}
#endif

	tmp = xmlnode_get_tag(xcfg, "rate");
	if (tmp != NULL) {
		DEF_RATE_TIME = j_atoi(xmlnode_get_attrib(tmp, "time"), 1);
		DEF_RATE_CONN =
		    j_atoi(xmlnode_get_attrib(tmp, "points"), 2);
		DEF_CONN = j_atoi(xmlnode_get_attrib(tmp, "count"), 500);

		log_alert("config",
			  "Allowed %d connections in %d seconds from one ip",
			  DEF_RATE_CONN, DEF_RATE_TIME);
		log_alert("config", "Allowed %d connections from one IP",
			  DEF_CONN);
	}

	wpj->cfg->max_clients =
	    j_atoi(xmlnode_get_tag_data(xcfg, "max_clients"),
		   DEF_MAX_CLIENTS - 5);

	wpj->cfg->epoll_size =
	    j_atoi(xmlnode_get_tag_data(xcfg, "epoll_size"),
		   DEF_EPOLL_SIZE);
	wpj->cfg->epoll_maxevents =
	    j_atoi(xmlnode_get_tag_data(xcfg, "epoll_maxevents"),
		   DEF_EPOLL_MAXEVENTS);

	log_alert("config", "epoll configured to %d sockets, %d events",
		  wpj->cfg->epoll_size, wpj->cfg->epoll_maxevents);

	if (wpj->cfg->max_clients > DEF_MAX_CLIENTS - 5)
		wpj->cfg->max_clients = DEF_MAX_CLIENTS - 5;

	log_alert("config", "configured to accept maximum %d connections",
		  wpj->cfg->max_clients);

	xmlnode_free(xcfg);
	return 1;
}

/* Main */
int main(int argc, char **argv)
{
	int j, help;
	char *c;
	char *cfgfile = NULL;
	int ret;
	pool p;
	int fd;
	char pidstr[10];

	/* Command line */
	help = 0;
	for (j = 1; j < argc; j++) {
		if (argv[j][0] != '-') {	/* make sure it's a valid command */
			help = 1;
			break;
		}

		c = argv[j] + 1;
		if (*c == 'c') {
			j++;
			cfgfile = argv[j];
			continue;
		}

		if (*c == 'D') {
			debug_flag = 1;
			continue;
		}

		if (*c == 'V' || *c == 'v') {
			printf("%s\n", VERSION);
			exit(0);
		}
	}

	if (help) {
		printf
		    ("USAGE:  %s -c\t\t configuration file -D\t\tenable debug -v\t\tserver version\n -V\t\tserver version\n\n",
		     argv[0]);
		exit(0);
	}

	/* alloc main structure */
	p = pool_heap(2048);
	wpj = pmalloco(p, sizeof(_wpjs));
	wpj->p = p;

	/* process config file */
	wpj->cfg = pmalloco(wpj->p, sizeof(_config));
	ret = configure(cfgfile ? cfgfile : "wpj.xml");
	if (ret == 0) {
		printf("Error parsing config file");
		pool_free(wpj->p);
		exit(1);
	}

	/* init clients memory */
	/* disable j 0 and 1 due to listen and server socket */
	for (j = 2; j < DEF_MAX_CLIENTS; j++) {
		client_c ci;
		ci = &(wpj->clients[j]);

		ci->m = NULL;
		ci->next = NULL;
		ci->loc = j;

		if (wpj->last_free_client == NULL)
			wpj->last_free_client = ci;
		else
			wpj->first_free_client->next = ci;

		wpj->first_free_client = ci;
	}

	/* create pidfile */
	fd = open(wpj->cfg->pidfile, O_CREAT | O_EXCL | O_WRONLY, 0600);
	if (fd < 0) {
		if (errno == EEXIST) {
			printf("Pidfile exist. Remove it first.");
			pool_free(wpj->p);
			exit(1);
		}
		printf("Error while openning pidfile.");
		pool_free(wpj->p);
		exit(1);
	}
	snprintf(pidstr, 10, "%d", getpid());
	write(fd, pidstr, strlen(pidstr));
	close(fd);

	log_alert(wpj->cfg->hostname,
		  "...___---:::=== Starting up ===:::---___...");

	/* ingore signals */
	signal(SIGPIPE, SIG_IGN);

	/* handle signals */
	signal(2, signal_end);
	signal(3, signal_end);
	signal(SIGTERM, signal_end);

	/* init IO */
	io_init();

	/* connect */
	connect_serwer();

	/* wait for connections */
	sleep(1);

	/* start listening */
	io_listen(wpj->cfg->lport, wpj->cfg->lip, client_process_xml,
		  NULL);

	log_alert("config", "Listening connections on %s:%d",
		  wpj->cfg->lip, wpj->cfg->lport);

	update_links_count();

	j = 0;
	while (wpj_shutdown != 1) {
		/* 5 sec */
		if (j > 10) {
			update_links_count();
			j = 0;
		}
		j++;
		Sleep(500);
	}

	log_alert("Signal", "Recieved Kill. Shutting down...");

	/* stop */
	io_stop();

	if (wpj->cfg->ssl)
		io_ssl_stop();

	update_links_count();

	/* remove pid file */
	if (wpj->cfg->pidfile != NULL) {
		unlink(wpj->cfg->pidfile);
	}

	pool_free(wpj->p);

	stop_log();

	return 0;
}

/* return miliseconds */
DWORD timeGetTime()
{
	struct timeval tv;
	DWORD ret;
	gettimeofday(&tv, NULL);
	ret = tv.tv_sec * 1000 + tv.tv_usec / 1000;
	return ret;
}

/* return 1/100 sec */
DWORD timeGetTimeSec100()
{
	struct timeval tv;
	DWORD ret;
	gettimeofday(&tv, NULL);
	ret = tv.tv_sec * 100 + tv.tv_usec / 10000;
	return ret;
}

time_t timeGetTimeSec()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec;
}

void Sleep(DWORD time)
{
	usleep(time * 1000);
}
