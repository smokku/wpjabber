/* --------------------------------------------------------------------------

 WPDNSRV module. Based on dnsrv.

 Modified by £ukasz Karwacki

 * --------------------------------------------------------------------------*/

#include "jabberd.h"
#include "srv_resolv.h"

/* Config format:
   <dnsrv xmlns='jabber:config:dnsrv'>
       <resend service="_jabber._tcp">foo.org</resend>
      ...

	<!-- CACHE -->
	<cashesize>1999</cashesize>
	<cachetimeout>5000</cachetimeout>
	<unresolvedtimeout>30</unresolvedtimeout>

	<!-- RESOLVER -->
	<queuesize>101</queuesize>
	<queuetimeout>30</queuetimeout>

   </dnsrv>

   Notes:
   * You must specify the services in the order you want them tried
*/

/* ------------------------------------------------- */
/* Struct to store list of services and resend hosts */
typedef struct __dns_resend_list {
	char *service;
	char *host;
	struct __dns_resend_list *next;
} *dns_resend_list, _dns_resend_list;


/* ----------------------------------------------------------- */
/* Struct to store list of dpackets which need to be delivered */
typedef struct __dns_packet_list {
	WPHASH_BUCKET;
	char *host;
	dpacket packet;
	struct __dns_packet_list *next;	/* next packet for the same host */
	struct __dns_packet_list *all_next, *all_prev;	/* list of all packets */
	time_t stamp;		/* time packet was recived */
	volatile int resolving;	/* 0, 1 resolving, 2 expired */
} *dns_packet_list, _dns_packet_list;

/* ----------------------------------------------------------- */
/* Struct to store cached names */
typedef struct dns_cacher_struct {
	WPHASH_BUCKET;
	pool p;
	char *ip;
	char *host;
	char *resendhost;
	time_t stamp;
} *dns_cacher, _dns_cacher;

typedef struct wpdns_struct *wpdns, _wpdns;

/* resolver thread struct */
typedef struct wpdns_thread_struct {
	pthread_t t;
	wpdns wd;
} *wpdns_thread, _wpdns_thread;

/* wpdns main struct */
struct wpdns_struct {
	instance i;
	volatile int shutdown;
	int thread_count;	/* not used now */
	wpxht packet_table;	/* Hash of dns_packet_lists */
	time_t packet_timeout;	/* how long to keep packets in the queue */
	SEM_VAR packet_sem;
	COND_VAR packet_cond;
	wpxht cache_table;	/* Hash of resolved IPs */
	time_t cache_timeout;	/* how long to keep resolutions in the cache */
	time_t unresolved_timeout;	/* how long to keep unresolved in the cache */
	SEM_VAR cache_sem;
	pool p;
	dns_resend_list svclist;
	dns_packet_list first, last;
	wpdns_thread threads;
};

inline void wpdns_add_element(wpdns wd, dns_packet_list l)
{
	/* first element */
	if (wd->last == NULL)
		wd->last = l;
	else
		wd->first->all_prev = l;

	l->all_next = wd->first;
	wd->first = l;
}



inline void wpdns_remove_element(wpdns wd, dns_packet_list l)
{

	if (l->all_prev) {
		l->all_prev->all_next = l->all_next;
	}
	if (l->all_next) {
		l->all_next->all_prev = l->all_prev;
	}
	if (wd->first == l) {
		wd->first = l->all_next;
	}
	if (wd->last == l) {
		wd->last = l->all_prev;
	}
	l->all_next = l->all_prev = NULL;
	return;

}

/* resend packet */
void dnsrv_resend(xmlnode pkt, char *ip, char *to)
{
	if (ip != NULL) {
		pkt = xmlnode_wrap(pkt, "route");
		xmlnode_put_attrib(pkt, "to", to);
		xmlnode_put_attrib(pkt, "ip", ip);
	} else {
		jutil_error(pkt, (terror) {
			    502, "Unable to resolve hostname."}
		);
		xmlnode_put_attrib(pkt, "iperror", "");
	}
	deliver(dpacket_new(pkt), NULL);
}


/* main resolving thread */
void *wpdns_thread_main(void *arg)
{
	wpdns_thread t = (wpdns_thread) arg;
	wpdns wd = t->wd;
	dns_packet_list l, temp;
	dns_resend_list iternode;
	dns_cacher c;
	char *str;
	pool p;

	log_debug("WPDNS thread start");

	while (1) {

		/* check if something to resolv */
		SEM_LOCK(wd->packet_sem);
		if (wd->last == NULL) {
			COND_WAIT(wd->packet_cond, wd->packet_sem);
		}

		/* get element */
		if (wd->last != NULL) {
			l = wd->last;
			l->resolving = 1;	/* mark it, not to remove it */
			wpdns_remove_element(wd, l);	/* remove from list */
		} else
			l = NULL;

		SEM_UNLOCK(wd->packet_sem);

		/* resolve */
		if (l) {

			log_debug("Start resolving for %s", l->host);

			iternode = wd->svclist;
			str = NULL;
			while (iternode != NULL) {
				str =
				    srv_lookup(l->packet->p,
					       iternode->service, l->host);
				if (str != NULL) {
					log_debug
					    ("Resolved %s(%s): %s\tresend to:%s",
					     l->host, iternode->service,
					     str, iternode->host);
					break;
				}
				iternode = iternode->next;
			}

			/* if resolved or cache unresolved */
			if ((str != NULL) || (wd->unresolved_timeout > 0)) {
				p = pool_heap(512);
				c = pmalloco(p, sizeof(_dns_cacher));
				c->p = p;
				c->stamp = time(NULL);
				c->host = pstrdup(p, l->host);

				if (str) {
					c->ip = pstrdup(p, str);
					c->resendhost =
					    pstrdup(p, iternode->host);
				}


				SEM_LOCK(wd->cache_sem);
				wpxhash_put(wd->cache_table, c->host,
					    (void *) c);
				SEM_UNLOCK(wd->cache_sem);
			}

			SEM_LOCK(wd->packet_sem);

			/* get latest one */
			l = wpxhash_get(wd->packet_table, l->host);

			/* remove */
			wpxhash_zap(wd->packet_table, l->host);

			SEM_UNLOCK(wd->packet_sem);

			/* send or free data */
			while (l != NULL) {
				temp = l;
				l = l->next;
				if (temp->resolving == 2)
					pool_free(temp->packet->p);
				else {
					if (str)
						dnsrv_resend(temp->packet->
							     x, str,
							     iternode->
							     host);
					else
						dnsrv_resend(temp->packet->
							     x, NULL,
							     NULL);
				}
			}
		}

		/* if end */
		if (wd->shutdown)
			break;
	}

	log_debug("WPDNS thread end");

	return NULL;
}


/* Hostname lookup requested */
void dnsrv_lookup(wpdns wd, dpacket p)
{
	dns_packet_list l, lnew;

	/* Attempt to lookup this hostname in the packet table */
	SEM_LOCK(wd->packet_sem);
	l = (dns_packet_list) wpxhash_get(wd->packet_table, p->host);

	/* if already in queue */
	if (l != NULL) {
		log_debug
		    ("dnsrv: Adding lookup request for %s to pending queue.",
		     p->host);
		lnew = pmalloco(p->p, sizeof(_dns_packet_list));
		lnew->packet = p;
		lnew->stamp = time(NULL);
		lnew->next = l;
		lnew->host = pstrdup(p->p, p->host);

		wpxhash_put(wd->packet_table, lnew->host, (void *) lnew);

		SEM_UNLOCK(wd->packet_sem);
		return;
	}

	/* insert the packet into the packet_table using the hostname
	   as the key and send a request to the coprocess */
	log_debug("dnsrv: Creating lookup request queue for %s", p->host);
	l = pmalloco(p->p, sizeof(_dns_packet_list));
	l->packet = p;
	l->stamp = time(NULL);
	l->host = pstrdup(p->p, p->host);
	wpxhash_put(wd->packet_table, l->host, l);

	/* add to list */
	wpdns_add_element(wd, l);

	COND_SIGNAL(wd->packet_cond);

	SEM_UNLOCK(wd->packet_sem);
}


result dnsrv_deliver(instance i, dpacket p, void *args)
{
	wpdns wd = (wpdns) args;
	dns_cacher c;
	jid to;

	/* if we get a route packet, it has to be to *us* and have the child as the real packet */
	if (p->type == p_ROUTE) {
		if (j_strcmp(p->host, i->id) != 0
		    || (to =
			jid_new(p->p,
				xmlnode_get_attrib(xmlnode_get_firstchild
						   (p->x), "to"))) == NULL)
			return r_ERR;
		p->x = xmlnode_get_firstchild(p->x);
		p->id = to;
		p->host = to->server;
	}

	/* Ensure this packet doesn't already have an IP */
	if (xmlnode_get_attrib(p->x, "ip")
	    || xmlnode_get_attrib(p->x, "iperror")) {
		log_notice(p->host,
			   "dropping looping dns lookup request: %s",
			   xmlnode2str(p->x));
		xmlnode_free(p->x);
		return r_DONE;
	}

	/* try the cache first */
	SEM_LOCK(wd->cache_sem);
	if ((c = wpxhash_get(wd->cache_table, p->host)) != NULL) {
		dnsrv_resend(p->x, c->ip, c->resendhost);
		SEM_UNLOCK(wd->cache_sem);
		return r_DONE;
	}
	SEM_UNLOCK(wd->cache_sem);


	dnsrv_lookup(wd, p);
	return r_DONE;
}

/* ====================================================== */
/* callback for walking the connecting hash tree */
int _dnsrv_beat_packets(void *arg, void *key, void *data)
{
	wpdns wd = (wpdns) arg;
	dns_packet_list l = (dns_packet_list) data;
	time_t now = time(NULL);

	for (l = (dns_packet_list) data; l; l = l->next) {
		/* if timeout */
		if (l->stamp + wd->packet_timeout < now) {
			/* mark tmeout */
			l->resolving = 2;
			/* send error */
			deliver_fail(dpacket_copy(l->packet),
				     "Hostname Resolution Timeout");
		}
	}

	return 1;
}

result dnsrv_beat_packets(void *arg)
{
	wpdns wd = (wpdns) arg;
	SEM_LOCK(wd->packet_sem);
	wpghash_walk(wd->packet_table, _dnsrv_beat_packets, arg);
	SEM_UNLOCK(wd->packet_sem);
	return r_DONE;
}


/* ====================================================== */
/* callback for walking the connecting hash tree */
int _dnsrv_beat_cache(void *arg, void *key, void *data)
{
	wpdns wd = (wpdns) arg;
	dns_cacher c = (dns_cacher) data;
	time_t now = time(NULL);

	if ((c->stamp + wd->cache_timeout < now) ||
	    ((c->ip == NULL)
	     && (c->stamp + wd->unresolved_timeout < now))) {
		log_debug("Remove from cache %s", key);
		wpxhash_zap(wd->cache_table, key);
		pool_free(c->p);
	}

	return 1;
}

result dnsrv_beat_cache(void *arg)
{
	wpdns wd = (wpdns) arg;
	SEM_LOCK(wd->cache_sem);
	wpghash_walk(wd->cache_table, _dnsrv_beat_cache, arg);
	SEM_UNLOCK(wd->cache_sem);
	return r_DONE;
}


void wpdns_shutdown(void *arg)
{
	wpdns wd = (wpdns) arg;
	void *ret;

	/* flag */
	wd->shutdown = 1;

	/* signal */
	COND_SIGNAL(wd->packet_cond);

	/* stop threads */
	pthread_join(wd->threads->t, &ret);


	/* XXX clean packet queues */

}


/* init */
void dnsrv(instance i, xmlnode x)
{
	xdbcache xc = NULL;
	xmlnode config = NULL;
	xmlnode iternode;
	dns_resend_list tmplist = NULL;
	wpdns wd;

	wd = pmalloco(i->p, sizeof(_wpdns));
	wd->i = i;
	wd->p = i->p;

	/* Load config from xdb */
	xc = xdb_cache(i);
	config =
	    xdb_get(xc, jid_new(xmlnode_pool(x), "config@-internal"),
		    "jabber:config:dnsrv");

	/* Build a list of services/resend hosts */
	iternode = xmlnode_get_lastchild(config);
	while (iternode != NULL) {
		if (j_strcmp("resend", xmlnode_get_name(iternode)) != 0) {
			iternode = xmlnode_get_prevsibling(iternode);
			continue;
		}

		/* Allocate a new list node */
		tmplist = pmalloco(wd->p, sizeof(_dns_resend_list));
		tmplist->service =
		    pstrdup(wd->p,
			    xmlnode_get_attrib(iternode, "service"));
		tmplist->host = pstrdup(wd->p, xmlnode_get_data(iternode));

		/* Insert this node into the list */
		tmplist->next = wd->svclist;
		wd->svclist = tmplist;

		/* Move to next child */
		iternode = xmlnode_get_prevsibling(iternode);
	}

	log_debug("dnsrv debug: %s\n", xmlnode2str(config));

	/* PACKET HASH */
	COND_INIT(wd->packet_cond);
	SEM_INIT(wd->packet_sem);
	wd->packet_table =
	    wpxhash_new(j_atoi
			(xmlnode_get_tag_data(config, "queuesize"), 101));
	wd->packet_timeout =
	    j_atoi(xmlnode_get_tag_data(config, "queuetimeout"), 60);
	wd->first = NULL;
	wd->last = NULL;
	log_alert("config", "Queue size: %d, timeout: %d",
		  wd->packet_table->prime, wd->packet_timeout);

	/* Setup the internal hostname cache */
	SEM_INIT(wd->cache_sem);
	wd->cache_table =
	    wpxhash_new(j_atoi
			(xmlnode_get_tag_data(config, "cachesize"), 1999));
	wd->cache_timeout =
	    j_atoi(xmlnode_get_tag_data(config, "cachetimeout"), 3600);
	wd->unresolved_timeout =
	    j_atoi(xmlnode_get_tag_data(config, "unresolvedtimeout"), 30);

	log_alert("config",
		  "Cache size: %d, timeout: %d, uresolved timeout: %d",
		  wd->cache_table->prime, wd->cache_timeout,
		  wd->unresolved_timeout);

	/* thread count */
	wd->thread_count =
	    j_atoi(xmlnode_get_attrib(config, "threads"), 1);
	wd->shutdown = 0;

	log_alert("config", "threads %d", wd->thread_count);

	xmlnode_free(config);

	/* shutdown */
	register_shutdown(wpdns_shutdown, (void *) wd);

	/* start threads */
	wd->threads = pmalloco(wd->p, sizeof(_wpdns_thread));
	wd->threads->wd = wd;
	pthread_create(&(wd->threads->t), NULL, wpdns_thread_main,
		       (void *) wd->threads);

	/* Register an incoming packet handler */
	register_phandler(i, o_DELIVER, dnsrv_deliver, (void *) wd);
	register_beat(60, dnsrv_beat_cache, (void *) wd);
	register_beat(30, dnsrv_beat_packets, (void *) wd);
}
