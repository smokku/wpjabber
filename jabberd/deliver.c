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

/* WARNING: the comments in here are random ramblings across the timespan this file has lived, don't rely on them for anything except entertainment (and if this entertains you, heh, you need to get out more :)

<jer mode="pondering">

ok, the whole <xdb/> <log/> <service/> and id="" and <host/> have gotten us this far, barely, but it needs some rethought.

there seem to be four types of conversations, xdb, log, route, and normal packets
each type can be sub-divided based on different criteria, such as hostname, namespace, log type, etc.

to do this right, base modules need to be able to assert their own logic within the delivery process
and we need to do it efficiently
and logically, so the administrator is able to understand where the packets are flowing

upon startup, like normal configuration directive callbacks, base modules can register a filter config callback with an arg per-type (xdb/log/service)
configuration calls each one of those, which use the arg to identify the instance that they are within
this one is special, jabberd tracks which instances each callback is associated with based on
during configuration, jabberd tracks which base modules were called
so during the configuration process, each base module registers a callback PER-INSTANCE that it's configured in

first, break down by
first step with a packet to be delivered is identify the instance it belongs to

filter_host
filter_ns
filter_logtype

it's an & operation, host & ns must have to match

first get type (xdb/log/svc)
then find which filters for the list of instances
then ask each filter to return a sub-set
after all the filters, deliver to the final instance(s)

what if we make <route/> addressing seperate, and only use the id="" for it?

we need to add a <drain> ... <drain> which matches any undelivered packet, and can send to another jabberd via accept/exec/stdout/etc
</jer>

<jer mode="pretty sure">

id="" is only required on services

HT host_norm
HT host_xdb
HT host_log
HT ns
ilist log_notice, log_alert, log_warning

to deliver the dpacket, first check the right host hashtable and get a list
second, if it's xdb or log, check the ns HT or log type list
find intersection of lists

if a host, ns, or type is used in any instance, it must be used in ALL of that type, or configuration error!

if intersection has multiple results, fail, or none, find uplink or fail

deliver()
	deliver_norm
		if(host_norm != NULL) ilista = host_norm(host)
	deliver_xdb
		if(host_xdb != NULL) ilista = host_xdb(host)
		if(ns != NULL) ilistb = ns(namespace)
		i = intersect(ilista, ilistb)
			if result multiple, return NULL
			if result single, return
			if result NULL, return uplink
		deliver_instance(i)
	deliver_log
		host_log, if logtype_flag switch on type
</jer> */

/*
   modified by Lukas Karwacki
   registering and unregistering instances are thread safe now
*/


#include "jabberd.h"

extern pool jabberd__runtime;

typedef struct ilist_struct {
	struct {
		WPHASH_BUCKET;
	};
	instance i;
	struct ilist_struct *next;
} *ilist, _ilist;


typedef struct rebuild_struct {
	wpxht h;
	pool p;
} *rebuild_p, rebuild_t;

ilist ilist_add(ilist il, instance i, pool p)
{
	ilist cur, ilnew;

	for (cur = il; cur != NULL; cur = cur->next)
		if (cur->i == i)
			return il;

	ilnew = pmalloco(p, sizeof(_ilist));
	ilnew->i = i;
	ilnew->next = il;
	return ilnew;
}

ilist ilist_rem(ilist il, instance i)
{
	ilist cur;

	if (il == NULL)
		return NULL;

	if (il->i == i)
		return il->next;

	for (cur = il; cur->next != NULL; cur = cur->next)
		if (cur->next->i == i) {
			cur->next = cur->next->next;
			return il;
		}

	return il;
}

/* hash table rebuild when add/remove instance */
void deliver_hashtable_rebuild(wpxht h, char *key, void *val, void *arg)
{
	ilist a, cur;
	rebuild_p r = (rebuild_p) arg;
	pool p = r->p;
	wpxht h2 = r->h;
	char *nkey;

	a = NULL;
	for (cur = (ilist) val; cur != NULL; cur = cur->next) {
		a = ilist_add(a, cur->i, p);
	}

	nkey = pstrdup(p, key);
	wpxhash_put(h2, nkey, a);
}

/* remove old hash after change */
result deliver_hashtable_free(void *arg)
{
	rebuild_p r = (rebuild_p) arg;
	wpxhash_free(r->h);;
	pool_free(r->p);
	free(r);
	return r_UNREG;
}

jabber_deliver j__deliver;

/* utility to find the right hashtable based on type */
inline HASHTABLE deliver_hashtable(ptype type)
{
	switch (type) {
	case p_LOG:
		return j__deliver->deliver__hlog;
	case p_XDB:
		return j__deliver->deliver__hxdb;
	default:
		return j__deliver->deliver__hnorm;
	}
}

inline pool deliver_pool(ptype type)
{
	switch (type) {
	case p_LOG:
		return j__deliver->hlog_p;
	case p_XDB:
		return j__deliver->hxdb_p;
	default:
		return j__deliver->hnorm_p;
	}
}

inline void set_deliver_hash_table(ptype type, wpxht ht_new, pool p)
{
	switch (type) {
	case p_LOG:
		j__deliver->deliver__hlog = ht_new;
		j__deliver->hlog_p = p;
		return;
	case p_XDB:
		j__deliver->deliver__hxdb = ht_new;
		j__deliver->hxdb_p = p;
		return;
	default:
		j__deliver->deliver__hnorm = ht_new;
		j__deliver->hnorm_p = p;
		return;
	}
}

inline int deliver_hashtable_size(ptype type)
{
	switch (type) {
	case p_LOG:
		return 31;
	case p_XDB:
		return 31;
	default:
		return 107;
	}
}

/* utility to find the right ilist in the hashtable */
inline ilist deliver_hashmatch(HASHTABLE ht, char *key)
{
	ilist l;
	l = wpxhash_get(ht, key);
	if (l == NULL) {
		l = wpxhash_get(ht, "*");
	}
	return l;
}

/* find and return the instance intersecting both lists, or react intelligently */
inline instance deliver_intersect(ilist a, ilist b)
{
	ilist cur = NULL, cur2;
	instance i = NULL;

	if (a == NULL)
		cur = b;
	if (b == NULL)
		cur = a;

	if (cur != NULL) {	/* we've only got one list */
		if (cur->next != NULL)
			return NULL;	/* multiple results is a failure */
		else
			return cur->i;
	}

	for (cur = a; cur != NULL; cur = cur->next) {
		for (cur2 = b; cur2 != NULL; cur2 = cur2->next) {
			if (cur->i == cur2->i) {	/* yay, intersection! */
				if (i != NULL)
					return NULL;	/* multiple results is a failure */
				i = cur->i;
			}
		}
	}

	if (i == NULL)		/* no match, use uplink */
		return j__deliver->deliver__uplink;

	return i;
}

/* special case handler for xdb calls @-internal */
void deliver_internal(dpacket p, instance i)
{
	xmlnode x;
	char *ns = xmlnode_get_attrib(p->x, "ns");

	log_debug("@-internal processing %s", xmlnode2str(p->x));

	if (j_strcmp(p->id->user, "config") == 0) {	/* config@-internal means it's a special xdb request to get data from the config file */
		for (x = xmlnode_get_firstchild(i->x); x != NULL;
		     x = xmlnode_get_nextsibling(x)) {
			if (j_strcmp(xmlnode_get_attrib(x, "xmlns"), ns) !=
			    0)
				continue;

			/* insert results */
			xmlnode_insert_tag_node(p->x, x);
		}

		/* reformat packet as a reply */
		xmlnode_put_attrib(p->x, "type", "result");
		jutil_tofrom(p->x);
		p->type = p_NORM;

		/* deliver back to the sending instance */
		deliver_instance(i, p);
		return;
	}

	if (j_strcmp(p->id->user, "host") == 0) {	/* dynamic register_instance crap */
		register_instance(i, p->id->resource);
		return;
	}

	if (j_strcmp(p->id->user, "unhost") == 0) {	/* dynamic register_instance crap */
		unregister_instance(i, p->id->resource);
		return;
	}
}

/* register this instance as a possible recipient of packets to this host */
void register_instance(instance i, char *host)
{
	ilist l;
	rebuild_p r;
	wpxht ht, ht_new;
	pool p_new;

	log_debug("Registering %s with instance %s", host, i->id);

	/* fail, since ns is required on every XDB instance if it's used on any one */
	if (i->type == p_XDB && j__deliver->deliver__ns != NULL
	    && xmlnode_get_tag(i->x, "ns") == NULL) {
		fprintf(stderr,
			"Configuration Error!  If <ns> is used in any xdb section, it must be used in all sections for correct packet routing.");
		exit(1);
	}
	/* fail, since logtype is required on every LOG instance if it's used on any one */
	if (i->type == p_LOG && j__deliver->deliver__logtype != NULL
	    && xmlnode_get_tag(i->x, "logtype") == NULL) {
		fprintf(stderr,
			"Configuration Error!  If <logtype> is used in any log section, it must be used in all sections for correct packet routing.");
		exit(1);
	}

	r = malloc(sizeof(rebuild_t));

	SEM_LOCK(j__deliver->deliver__sem);

	ht = deliver_hashtable(i->type);

	/* copy hash table */
	ht_new = wpxhash_new(deliver_hashtable_size(i->type));
	p_new = pool_heap(2048);

	r->h = ht_new;
	r->p = p_new;

	wpxhash_walk(ht, deliver_hashtable_rebuild, r);

	/* add element to new hash */
	l = wpxhash_get(ht_new, host);
	l = ilist_add(l, i, p_new);
	wpxhash_put(ht_new, pstrdup(p_new, host), (void *) l);

	r->h = ht;
	r->p = deliver_pool(i->type);

	/* replace old hash */
	set_deliver_hash_table(i->type, ht_new, p_new);

	SEM_UNLOCK(j__deliver->deliver__sem);

	/* free old hash */
	if (ht != NULL) {
		register_beat(20, deliver_hashtable_free, (void *) r);
	} else {
		free(r);
	}
}

void unregister_instance(instance i, char *host)
{
	ilist l;
	wpxht ht, ht_new;
	pool p_new;
	rebuild_p r;

	log_debug("Unregistering %s with instance %s", host, i->id);

	r = malloc(sizeof(rebuild_t));

	SEM_LOCK(j__deliver->deliver__sem);

	ht = deliver_hashtable(i->type);

	/* copy hash table */
	ht_new = wpxhash_new(deliver_hashtable_size(i->type));
	p_new = pool_heap(2048);

	r->h = ht_new;
	r->p = p_new;

	wpxhash_walk(ht, deliver_hashtable_rebuild, r);

	/* remove element */
	l = wpxhash_get(ht_new, host);
	l = ilist_rem(l, i);
	if (l == NULL)
		wpxhash_zap(ht_new, host);
	else
		wpxhash_put(ht_new, pstrdup(p_new, host), (void *) l);

	r->h = ht;
	r->p = deliver_pool(i->type);

	/* replace old hash */
	set_deliver_hash_table(i->type, ht_new, p_new);

	SEM_UNLOCK(j__deliver->deliver__sem);

	/* free old hash */
	if (ht != NULL) {
		register_beat(20, deliver_hashtable_free, (void *) r);
	} else {
		free(r);
	}
}

result deliver_config_host(instance i, xmlnode x, void *arg)
{
	char *host;
	int c;

	if (i == NULL)
		return r_PASS;

	host = xmlnode_get_data(x);
	if (host == NULL) {
		register_instance(i, "*");
		return r_DONE;
	}

	for (c = 0; host[c] != '\0'; c++)
		if (isspace(host[c])) {
			xmlnode_put_attrib(x, "error",
					   "The host tag contains illegal whitespace.");
			return r_ERR;
		}

	register_instance(i, host);

	return r_DONE;
}

result deliver_config_ns(instance i, xmlnode x, void *arg)
{
	ilist l;
	wpxht ht, ht_new;
	pool p_new;
	rebuild_p r;
	char *ns, star[] = "*";

	if (i == NULL)
		return r_PASS;

	if (i->type != p_XDB)
		return r_ERR;

	ns = xmlnode_get_data(x);
	if (ns == NULL)
		ns = pstrdup(xmlnode_pool(x), star);

	log_debug("Registering namespace %s with instance %s", ns, i->id);

	r = malloc(sizeof(rebuild_t));

	SEM_LOCK(j__deliver->deliver__sem);

	ht = j__deliver->deliver__ns;
	ht_new = wpxhash_new(deliver_hashtable_size(p_XDB));
	p_new = pool_heap(2048);

	r->h = ht_new;
	r->p = p_new;

	if (ht != NULL) {
		wpxhash_walk(ht, deliver_hashtable_rebuild, r);
	}

	l = wpxhash_get(ht_new, ns);
	l = ilist_add(l, i, p_new);
	wpxhash_put(ht_new, pstrdup(p_new, ns), (void *) l);

	r->h = ht;
	r->p = j__deliver->ns_p;

	j__deliver->deliver__ns = ht_new;
	j__deliver->ns_p = p_new;

	SEM_UNLOCK(j__deliver->deliver__sem);

	/* free old hash */
	if (ht != NULL) {
		register_beat(20, deliver_hashtable_free, r);
	} else {
		free(r);
	}
	return r_DONE;
}

result deliver_config_logtype(instance i, xmlnode x, void *arg)
{
	ilist l;
	wpxht ht, ht_new;
	pool p_new;
	rebuild_p r;
	char *type, star[] = "*";

	if (i == NULL)
		return r_PASS;

	if (i->type != p_LOG)
		return r_ERR;

	type = xmlnode_get_data(x);
	if (type == NULL)
		type = pstrdup(xmlnode_pool(x), star);

	log_debug("Registering logtype %s with instance %s", type, i->id);

	r = malloc(sizeof(rebuild_t));

	SEM_LOCK(j__deliver->deliver__sem);

	ht = j__deliver->deliver__logtype;
	ht_new = wpxhash_new(deliver_hashtable_size(p_LOG));
	p_new = pool_heap(2048);

	r->h = ht_new;
	r->p = p_new;

	if (ht != NULL) {
		wpxhash_walk(ht, deliver_hashtable_rebuild, r);
	}

	l = wpxhash_get(ht_new, type);
	l = ilist_add(l, i, p_new);
	wpxhash_put(ht_new, pstrdup(p_new, type), (void *) l);

	r->h = ht;
	r->p = j__deliver->logtype_p;

	j__deliver->deliver__logtype = ht_new;
	j__deliver->logtype_p = p_new;

	SEM_UNLOCK(j__deliver->deliver__sem);

	/* free old hash */
	if (ht != NULL) {
		register_beat(20, deliver_hashtable_free, (void *) r);
	} else {
		free(r);
	}

	return r_DONE;
}

result deliver_config_uplink(instance i, xmlnode x, void *arg)
{
	if (i == NULL)
		return r_PASS;

	if (j__deliver->deliver__uplink != NULL)
		return r_ERR;

	j__deliver->deliver__uplink = i;
	return r_DONE;
}

/* NULL that sukkah! */
result deliver_null(instance i, dpacket p, void *arg)
{
	pool_free(p->p);
	return r_DONE;
}

result deliver_config_null(instance i, xmlnode x, void *arg)
{
	if (i == NULL)
		return r_PASS;

	register_phandler(i, o_DELIVER, deliver_null, NULL);
	return r_DONE;
}

void deliver(dpacket p, instance i)
{
	ilist a, b;

	/*
	   if(deliver__flag == 1 && p == NULL && i == NULL){
	   deliver_msg d;
	   while((d=(deliver_msg)pth_msgport_get(deliver__mp))!=NULL){
	   deliver(d->p,d->i);
	   }
	   pth_msgport_destroy(deliver__mp);
	   deliver__mp = NULL;
	   deliver__flag = -1;
	   }
	 */

	/* Ensure the packet is valid */
	if (p == NULL)
		return;

	/* catch the @-internal xdb crap */
	if (p->type == p_XDB && *(p->host) == '-')
		return deliver_internal(p, i);

	/*
	   if(deliver__flag == 0){
	   deliver_msg d = pmalloco(xmlnode_pool(p->x) ,sizeof(_deliver_msg));

	   if(deliver__mp == NULL)
	   deliver__mp = pth_msgport_create("deliver__");

	   d->i = i;
	   d->p = p;

	   pth_msgport_put(deliver__mp, (void*)d);
	   return;
	   }
	 */

	log_debug("DELIVER %d:%s %s", p->type, p->host, xmlnode2str(p->x));

	b = NULL;

	/* take ilist from hashtable */
	a = deliver_hashmatch(deliver_hashtable(p->type), p->host);
	if (p->type == p_XDB)
		b = deliver_hashmatch(j__deliver->deliver__ns,
				      xmlnode_get_attrib(p->x, "ns"));
	else if (p->type == p_LOG)
		b = deliver_hashmatch(j__deliver->deliver__logtype,
				      xmlnode_get_attrib(p->x, "type"));

	deliver_instance(deliver_intersect(a, b), p);
}

void deliver_init(void)
{
	j__deliver = pmalloco(jabberd__runtime, sizeof(_jabber_deliver));

	j__deliver->deliver__ns = NULL;
	j__deliver->deliver__logtype = NULL;
	j__deliver->deliver__uplink = NULL;
	j__deliver->deliver__flag = 0;

	SEM_INIT(j__deliver->deliver__sem);
	j__deliver->deliver__hnorm =
	    wpxhash_new(deliver_hashtable_size(p_NORM));
	j__deliver->hnorm_p = pool_heap(2048);

	j__deliver->deliver__hlog =
	    wpxhash_new(deliver_hashtable_size(p_LOG));
	j__deliver->hnorm_p = pool_heap(2048);

	j__deliver->deliver__hxdb =
	    wpxhash_new(deliver_hashtable_size(p_XDB));
	j__deliver->hxdb_p = pool_heap(2048);

	register_config("host", deliver_config_host, NULL);
	register_config("ns", deliver_config_ns, NULL);
	register_config("logtype", deliver_config_logtype, NULL);
	register_config("uplink", deliver_config_uplink, NULL);
	register_config("null", deliver_config_null, NULL);
}

/* register a function to handle delivery for this instance */
void register_phandler(instance id, order o, phandler f, void *arg)
{
	handel newh, h1, last;
	pool p;

	/* create handel and setup */
	p = pool_new();		/* use our own little pool */
	newh = pmalloc_x(p, sizeof(_handel), 0);
	newh->p = p;
	newh->f = f;
	newh->arg = arg;
	newh->o = o;

	/* if we're the only handler, easy */
	if (id->hds == NULL) {
		id->hds = newh;
		return;
	}

	/* place according to handler preference */
	switch (o) {
	case o_PRECOND:
		/* always goes to front of list */
		newh->next = id->hds;
		id->hds = newh;
		break;
	case o_COND:
		h1 = id->hds;
		last = NULL;
		while (h1->o < o_PREDELIVER) {
			last = h1;
			h1 = h1->next;
			if (h1 == NULL)
				break;
		}
		if (last == NULL) {	/* goes to front of list */
			newh->next = h1;
			id->hds = newh;
		} else if (h1 == NULL) {	/* goes at end of list */
			last->next = newh;
		} else {	/* goes between last and h1 */
			newh->next = h1;
			last->next = newh;
		}
		break;
	case o_PREDELIVER:
		h1 = id->hds;
		last = NULL;
		while (h1->o < o_DELIVER) {
			last = h1;
			h1 = h1->next;
			if (h1 == NULL)
				break;
		}
		if (last == NULL) {	/* goes to front of list */
			newh->next = h1;
			id->hds = newh;
		} else if (h1 == NULL) {	/* goes at end of list */
			last->next = newh;
		} else {	/* goes between last and h1 */
			newh->next = h1;
			last->next = newh;
		}
		break;
	case o_DELIVER:
		/* always add to the end */
		for (h1 = id->hds; h1->next != NULL; h1 = h1->next);
		h1->next = newh;
		break;
	}
}


/* bounce on the delivery, use the result to better gague what went wrong */
void deliver_fail(dpacket p, char *err)
{
	terror t;
	char message[1024];

	log_debug("delivery failed (%s)", err);

	if (p == NULL)
		return;

	switch (p->type) {
	case p_LOG:
		/* stderr and drop */
		snprintf(message, 1020, "WARNING!  Logging Failed: %s\n",
			 xmlnode2str(p->x));
		fprintf(stderr, "%s\n", message);
		pool_free(p->p);
		break;
	case p_XDB:
		/* log_warning and drop */
		log_warn(p->host, "dropping a %s xdb request to %s for %s",
			 xmlnode_get_attrib(p->x, "type"),
			 xmlnode_get_attrib(p->x, "to"),
			 xmlnode_get_attrib(p->x, "ns"));
		/* drop through and treat like a route failure */
	case p_ROUTE:
		/* route packet bounce */
		if (j_strcmp(xmlnode_get_attrib(p->x, "type"), "error") == 0) {	/* already bounced once, drop */
			log_warn(p->host,
				 "dropping a routed packet to %s from %s: %s",
				 xmlnode_get_attrib(p->x, "to"),
				 xmlnode_get_attrib(p->x, "from"), err);
			pool_free(p->p);
		} else {
			log_notice(p->host,
				   "bouncing a routed packet to %s from %s: %s",
				   xmlnode_get_attrib(p->x, "to"),
				   xmlnode_get_attrib(p->x, "from"), err);

			/* turn into an error and bounce */
			jutil_tofrom(p->x);
			xmlnode_put_attrib(p->x, "type", "error");
			xmlnode_put_attrib(p->x, "error", err);
			deliver(dpacket_new(p->x), NULL);
		}
		break;
	case p_NORM:
		/* normal packet bounce */
		if (j_strcmp(xmlnode_get_attrib(p->x, "type"), "error") == 0) {	/* can't bounce an error */
			log_warn(p->host,
				 "dropping a packet to %s from %s: %s",
				 xmlnode_get_attrib(p->x, "to"),
				 xmlnode_get_attrib(p->x, "from"), err);
			pool_free(p->p);
		} else {
			char *xto = xmlnode_get_attrib(p->x, "to");
			if (xto && !strchr(xto, '@')) {
				xmlnode_free(p->x);
				break;
			}
			log_notice(p->host,
				   "bouncing a packet to %s from %s: %s",
				   xto, xmlnode_get_attrib(p->x, "from"),
				   err);

			/* turn into an error */
			if (err == NULL) {
				jutil_error(p->x, TERROR_EXTERNAL);
			} else {
				t.code = 502;
				t.msg[0] = '\0';
				strcat(t.msg, err);	/* c sucks */
				jutil_error(p->x, t);
			}
			deliver(dpacket_new(p->x), NULL);
		}
		break;
	case p_NONE:
		break;
	}
}

/* actually perform the delivery to an instance */
void deliver_instance(instance i, dpacket p)
{
	handel h, hlast;
	result r;
	dpacket pig = p;

	if (i == NULL) {
		deliver_fail(p, "Unable to deliver, destination unknown");
		return;
	}

	log_debug("delivering to instance '%s'", i->id);

	/* try all the handlers */
	hlast = h = i->hds;
	while (h != NULL) {
		/* there may be multiple delivery handlers, make a backup copy first if we have to */
		if (h->o == o_DELIVER && h->next != NULL)
			pig = dpacket_copy(p);

		/* call the handler */
		if ((r = (h->f) (i, p, h->arg)) == r_ERR) {
			deliver_fail(p, "Internal Delivery Error");
			break;
		}

		/* if a non-delivery handler says it handled it, we have to be done */
		if (h->o != o_DELIVER && r == r_DONE)
			break;

		/* if a conditional handler wants to halt processing */
		if (h->o == o_COND && r == r_LAST)
			break;

		/* deal with that backup copy we made */
		if (h->o == o_DELIVER && h->next != NULL) {
			if (r == r_DONE)	/* they ate it, use copy */
				p = pig;
			else
				pool_free(pig->p);	/* they never used it, trash copy */
		}

		/* unregister this handler */
		if (r == r_UNREG) {
			/* XXX make it thread safe */

			if (h == i->hds) {	/* removing the first in the list */
				i->hds = h->next;
				pool_free(h->p);
				hlast = h = i->hds;
			} else {	/* removing from anywhere in the list */
				hlast->next = h->next;
				pool_free(h->p);
				h = hlast->next;
			}
			continue;
		}

		hlast = h;
		h = h->next;
	}

}

dpacket dpacket_new(xmlnode x)
{
	dpacket p;
	char *str;

	if (x == NULL)
		return NULL;

	/* create the new packet */
	p = pmalloc_x(xmlnode_pool(x), sizeof(_dpacket), 0);
	p->x = x;
	p->p = xmlnode_pool(x);

	/* determine it's type */
	p->type = p_NORM;
	if (*(xmlnode_get_name(x)) == 'r')
		p->type = p_ROUTE;
	else if (*(xmlnode_get_name(x)) == 'x')
		p->type = p_XDB;
	else if (*(xmlnode_get_name(x)) == 'l')
		p->type = p_LOG;

	/* xdb results are shipped as normal packets */
	if (p->type == p_XDB
	    && (str = xmlnode_get_attrib(p->x, "type")) != NULL
	    && (*str == 'r' || *str == 'e'))
		p->type = p_NORM;

	/* determine who to route it to, overriding the default to="" attrib only for logs where we use from */
	if (p->type == p_LOG)
		p->id = jid_new(p->p, xmlnode_get_attrib(x, "from"));
	else
		p->id = jid_new(p->p, xmlnode_get_attrib(x, "to"));

	if (p->id == NULL) {
		log_warn(NULL,
			 "Packet Delivery Failed, invalid packet 'to', dropping %s",
			 xmlnode2str(x));
		xmlnode_free(x);
		return NULL;
	}

	/* make sure each packet has the basics, norm has a to/from, log has a type, xdb has a namespace */
	switch (p->type) {
	case p_LOG:
		if (xmlnode_get_attrib(x, "type") == NULL)
			p = NULL;
		break;
	case p_XDB:
		if (xmlnode_get_attrib(x, "ns") == NULL)
			p = NULL;
		/* fall through */
	case p_NORM:
		if (xmlnode_get_attrib(x, "to") == NULL
		    || xmlnode_get_attrib(x, "from") == NULL)
			p = NULL;
		break;
	case p_ROUTE:
		if (xmlnode_get_attrib(x, "to") == NULL)
			p = NULL;
		break;
	case p_NONE:
		p = NULL;
		break;
	}
	if (p == NULL) {
		log_warn(NULL,
			 "Packet Delivery Failed, invalid packet, dropping %s",
			 xmlnode2str(x));
		xmlnode_free(x);
		return NULL;
	}

	p->host = p->id->server;
	return p;
}

dpacket dpacket_copy(dpacket p)
{
	dpacket p2;

	p2 = dpacket_new(xmlnode_dup(p->x));
	return p2;
}
