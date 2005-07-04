/* --------------------------------------------------------------------------
 *
 * License
 *
 * The contents of this file are subject to the Jabber Open Source License
 * Version 1.0 (the "JOSL").  You may not copy or use this file, in either
 * source code or executable form, except in compliance with the JOSL. You
 * may obtain a copy of the JOSL at http://www.jabber.org/ or at
 * http://www.opensource.org/.
 *
 * Software distributed under the JOSL is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied.  See the JOSL
 * for the specific language governing rights and limitations under the
 * JOSL.
 *
 * Copyrights
 *
 * Portions created by or assigned to Jabber.com, Inc. are
 * Copyright (c) 1999-2002 Jabber.com, Inc.  All Rights Reserved.  Contact
 * information for Jabber.com, Inc. is available at http://www.jabber.com/.
 *
 * Portions Copyright (c) 1998-1999 Jeremie Miller.
 *
 * Acknowledgements
 *
 * Special thanks to the Jabber Open Source Contributors for their
 * suggestions and support of Jabber.
 *
 * Alternatively, the contents of this file may be used under the terms of the
 * GNU General Public License Version 2 or later (the "GPL"), in which case
 * the provisions of the GPL are applicable instead of those above.  If you
 * wish to allow use of your version of this file only under the terms of the
 * GPL and not to allow others to use your version of this file under the JOSL,
 * indicate your decision by deleting the provisions above and replace them
 * with the notice and other provisions required by the GPL.  If you do not
 * delete the provisions above, a recipient may use your version of this file
 * under either the JOSL or the GPL.
 *
 *
 * --------------------------------------------------------------------------*/

/*

  JSM - Modifed by £ukasz Karwacki <lukasm@wp-sa.pl>

changes:
1. jsm works only for one domain
2. removed mod_filter,mod_groups I don't use them


*/
#ifdef WIN32
#ifdef JSM_EXPORT
#define JSM_FUNC __declspec (dllexport)
#else
#define JSM_FUNC __declspec (dllimport)
#endif
#else
#define JSM_FUNC
#endif

#include "jabberd.h"

#define MOD_VERSION "jsm version 1.1.4 stable for pthreaded server"

#define TERROR_USERNAMENOTAVAILABLE  (terror){409,"Username Not Available"}

/* only 3 different resources are allowed to login
 you can change it */
#define MAX_USER_SESSIONS 10

/* set to a prime number larger then the average max # of hosts, and another for the max # of users for any single host */
//#define HOSTS_PRIME 17
#define USERS_PRIME 3001

/* master event types */
typedef int event;
#define e_SESSION  0		/* when a session is starting up */
#define e_OFFLINE  1		/* data for an offline user */
#define e_SERVER   2		/* packets for the server.host */
#define e_DELIVER  3		/* about to deliver a packet to an mp */
#define e_SHUTDOWN 4		/* server is shutting down, last chance! */
#define e_AUTH	   5		/* authentication handlers */
#define e_REGISTER 6		/* registration request */
/* always add new event types here, to maintain backwards binary compatibility */
#define e_LAST	   7		/* flag for the highest */

/* session event types */
#define es_IN	   0		/* for packets coming into the session */
#define es_OUT	   1		/* for packets originating from the session */
#define es_END	   2		/* when a session ends */
/* always add new event types here, to maintain backwards binary compatibility */
#define es_POSTOUT 3		/* when a session ends */
#define es_LAST    4		/* flag for the highest */

/* admin user account flags */
#define ADMIN_UNKNOWN	0x00
#define ADMIN_NONE	0x01
#define ADMIN_READ	0x02
#define ADMIN_WRITE	0x04


typedef enum { M_PASS,		/* we don't want this packet this time */
	M_IGNORE,		/* we don't want this packet ever */
	M_HANDLED		/* stop mapi processing on this packet */
} mreturn;

typedef struct udata_struct *udata, _udata;
typedef struct session_struct *session, _session;
typedef struct jsmi_struct *jsmi, _jsmi;

typedef struct mapi_struct {
	jsmi si;
	jpacket packet;
	event e;
	udata user;
	session s;
} *mapi, _mapi;

typedef mreturn(*mcall) (mapi m, void *arg);

typedef struct mlist_struct {
	mcall c;
	void *arg;
	unsigned char mask;
	struct mlist_struct *next;
} *mlist, _mlist;

typedef struct jsmi_stats_struct {
	unsigned int sessioncount;
	unsigned int usercount;
	unsigned int session_max_today;	/* highest sesion count from last day */
	unsigned int session_max_yesterday;	/* highest sesion count from last day */
	unsigned int last_max_day;
	unsigned int users_registered_today;
	unsigned int users_registered_from_start;
	unsigned long packets_in;
	unsigned long packets_out;
	time_t started;
	char *stats_file;
} _jsmi_stats, *jsmi_stats;

#define DEFAULT_DELIVER_THREADS 2
#define DEFAULT_AUTHREG_THREADS 3
#define DEFAULT_SESSION_THREADS 10

typedef struct fast_mtq_queue {
	fast_mtq mtq;
	int count;
} *jsm_mtq_queue_p, jsm_mtq_queue_t;

/* globals for this instance of jsm */
struct jsmi_struct {
	instance i;
	pool p;
	xmlnode config;

	wpxht users;
	SEM_VAR sem;

	char *host;		/* our host */
	xdbcache xc;
	mlist events[e_LAST];
	jid gtrust;		/* globaly trusted jids */
	jsmi_stats stats;

	fast_mtq mtq_deliver;
	fast_mtq mtq_authreg;
	jsm_mtq_queue_p *mtq_session;
	int mtq_session_count;

	int shutdown;
};

struct udata_struct {		/*  56 B */
	WPHASH_BUCKET;
	char *user;
	char *pass;
	jid id;
	volatile void *roster_cache;
	jsmi si;
	pool p;
	struct udata_struct *next;
	volatile session sessions;
	SEM_VAR sem;		/* sessions sem */
	volatile unsigned int ref;
	volatile unsigned int scount;
	int removed;
};

struct session_struct {		/*  60B */
	struct session_struct *next;
	/* general session data */
	jsmi si;
	char *res;
	char *ip;
	volatile xmlnode presence;

	/* mechanics */
	pool p;
	mlist events[es_LAST];

	jsm_mtq_queue_p q;	/* thread queue */

	udata u;
	jid id;
	jid uid;		/* id without resource */

	/* our routed id, and remote session id */
	jid route;
	jid sid;

	time_t started;

	volatile unsigned int exit_flag;
	unsigned int roster;	/* flag that sesion got roster */

	volatile int priority;
	int c_in, c_out;
};

typedef struct packet_thread_struct {
	FAST_MTQ_ELEMENT;
	union {
		void *arg1;
		jpacket jp;
		session s;
	};
	union {
		void *arg2;
		dpacket p;
		int type;
	};
} *packet_thread_p, packet_thread_t;

/* session */
session js_session_new(jsmi si, dpacket p);
void js_session_end(session s, char *reason);
void js_session_end_nosem(session s, char *reason);
session js_session_get(udata user, char *res);
session js_session_primary(udata user);
session js_session_primary_sem(udata user);
void js_session_to(session s, jpacket p);
void js_session_from(session s, jpacket p);

udata js_user(jsmi si, jid id, int online);

/* main events */
void js_post_out(void *arg);
void js_post_out_main(session s, jpacket jp);
void js_server_main(jsmi si, jpacket jp);
void js_offline_main(jsmi si, jpacket jp);

/* external events */
JSM_FUNC result js_packet(instance i, dpacket p, void *arg);
JSM_FUNC result js_users_gc(void *arg);
JSM_FUNC result jsm_stats(void *arg);
JSM_FUNC void jsm_shutdown(void *arg);
JSM_FUNC void jsm_configure(jsmi si);

/* internal deliver */
void js_deliver(jsmi si, jpacket p);
//void js_psend(jsmi si, jpacket p, mtq_callback f);
void js_bounce(jsmi si, xmlnode x, terror terr);

/* module */
void js_mapi_register(jsmi si, event e, mcall c, void *arg);
void js_mapi_session(event e, session s, mcall c, void *arg);
int js_mapi_call(jsmi si, event e, jpacket packet, udata user, session s);

/* utils */
xmlnode js_config(jsmi si, char *query);
//int js_admin(udata u, int flag);
int js_admin_jid(jsmi si, jid from, int flag);
int js_islocal(jsmi si, jid id);
int js_trust(udata u, jid id);	/* checks if id is trusted by user u + globally trusted */
int js_istrusted(udata u, jid id);	/* checks if user is trusted by users in his roster */
void js_roster_save(udata u, xmlnode roster);
void js_trust_populate_presence(mapi m, jid notify);	/* for speeding up mod_presence */
int js_roster_item_group(udata u, jid id, char *group);
int js_online(mapi m);		/* logic to tell if this is a go-online call */
xmlnode util_offline_event(xmlnode old, char *from, char *to, char *id);
