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
#include "jabberd.h"


/* WP changes */
/*

 - Added sems
 - xdb_shutdown finishes xdb_get, xdb_set queries ( if they were by LAN )

*/

result xdb_results(instance id, dpacket p, void *arg)
{
	xdbcache xc = (xdbcache) arg;
	xdbrequest_p cur, last;
	unsigned int idnum;
	char *idstr;

	if (p->type != p_NORM || *(xmlnode_get_name(p->x)) != 'x')
		return r_PASS;	/* yes, we are matching ANY <x*> element */

	log_debug("xdb_results checking xdb packet %s", xmlnode2str(p->x));

	if ((idstr = xmlnode_get_attrib(p->x, "id")) == NULL)
		return r_ERR;

	idnum = atoi(idstr);

	SEM_LOCK(xc->sem);

	for (last = NULL, cur = xc->first;
	     (cur) && (cur->id != idnum); last = cur, cur = cur->next);

	if (!cur) {
		pool_free(p->p);
		SEM_UNLOCK(xc->sem);
		return r_DONE;
	}

	/* associte only a non-error packet w/ waiting cache */
	if (j_strcmp(xmlnode_get_attrib(p->x, "type"), "error") == 0) {
		cur->data = NULL;
		pool_free(p->p);
	} else
		cur->data = p->x;

	if (!last)
		xc->first = cur->next;
	else
		last->next = cur->next;

	cur->external = 0;
	SEM_UNLOCK(xc->sem);

	cur->preblock = 1;
	return r_DONE;
}

typedef struct xdb_resend_struct {
	instance i;
	xmlnode x;
} *xdb_resend, _xdb_resend;

/** Resend packet in other thread  */
void resend_xdb(void *arg)
{
	xdb_resend cur = (xdb_resend) arg;
	deliver(dpacket_new(cur->x), cur->i);
}

/* actually deliver the xdb request */
void xdb_deliver(instance i, xdbrequest_p cur, int another_thread)
{
	xmlnode x;
	char ids[15];

	x = xmlnode_new_tag("xdb");

	if (cur->set) {
		xmlnode_put_attrib(x, "type", "set");
		xmlnode_insert_tag_node(x, cur->data);	/* copy in the data */
		if (cur->act != NULL)
			xmlnode_put_attrib(x, "action", cur->act);
		if (cur->match != NULL)
			xmlnode_put_attrib(x, "match", cur->match);
	} else {
		xmlnode_put_attrib(x, "type", "get");
	}
	xmlnode_put_attrib(x, "to", jid_full(cur->owner));
	xmlnode_put_attrib(x, "from", i->id);
	xmlnode_put_attrib(x, "ns", cur->ns);
	sprintf(ids, "%d", cur->id);
	xmlnode_put_attrib(x, "id", ids);	/* to track response */

	if (another_thread) {
		xdb_resend xr =
		    pmalloco(xmlnode_pool(x), sizeof(_xdb_resend));
		xr->x = x;
		xr->i = i;
		mtq_send(NULL, NULL, resend_xdb, (void *) xr);
	} else {
		deliver(dpacket_new(x), i);
	}
}


void xdb_shutdown(void *arg)
{
	xdbcache xc = (xdbcache) arg;
	xdbrequest_p cur;

	log_debug("XDB SHUTDOWN");

	xc->shutdown = 1;

	SEM_LOCK(xc->sem);
	while (xc->first) {
		cur = xc->first;
		xc->first = cur->next;

		cur->data = NULL;
		cur->preblock = 1;
	}
	SEM_UNLOCK(xc->sem);
}

/** resend packets. heartbeat thread */
result xdb_thump(void *arg)
{
	xdbcache xc = (xdbcache) arg;
	xdbrequest_p cur, next, last;
	struct timeval tv;
	time_t now;

	if (xc->shutdown)
		return r_UNREG;

	gettimeofday(&tv, NULL);
	now = tv.tv_sec;

	SEM_LOCK(xc->sem);
	/* spin through the cache looking for stale requests */

	for (last = NULL, cur = xc->first; cur;) {
		next = cur->next;
		if ((cur->external) && ((now - cur->sent) > 30)) {

			if (!last)
				xc->first = next;
			else
				last->next = next;

			cur->data = NULL;
			cur->preblock = 1;
		} else {
			last = cur;

			if ((cur->external) && ((now - cur->sent) > 10)) {
				log_alert("xdb", "Resend xdb packet");
				xdb_deliver(xc->i, cur, 1);
			}
		}

		cur = next;
	}
	SEM_UNLOCK(xc->sem);

	return r_DONE;
}

xdbcache xdb_cache(instance id)
{
	xdbcache xc;

	if (id == NULL) {
		log_alert("xdb",
			  "Programming Error: xdb_cache() called with NULL\n");
		return NULL;
	}

	xc = pmalloco(id->p, sizeof(_xdbcache));
	xc->i = id;		/* flags it as the top of the ring too */
	xc->first = NULL;	/* init ring */
	SEM_INIT(xc->sem);

	/* register the handler in the instance to filter out xdb results */
	register_phandler(id, o_PRECOND, xdb_results, (void *) xc);

	/* heartbeat to keep a watchful eye on xdb_cache */
	register_beat(10, xdb_thump, (void *) xc);

	register_shutdown_first(xdb_shutdown, (void *) xc);
	return xc;
}

/* blocks until namespace is retrieved, host must map back to this service! */
xmlnode xdb_get(xdbcache xc, jid owner, char *ns)
{
	xdbrequest_p cur;
	xmlnode x;
	struct timeval tv;

	if (xc->shutdown)
		return NULL;

	if (xc == NULL || owner == NULL || ns == NULL) {
		if (ns) {
			log_alert("xdb_get",
				  "Programming Error: xdb_get() called with NULL ns: %s\n",
				  ns);
		} else {
			log_alert("xdb_get",
				  "Programming Error: xdb_get() called with NULL ns: NULL\n");
		}

		if (owner) {
			log_alert("owner", "%s", jid_full(owner));
		}
		return NULL;
	}

	log_debug("XDB GET");

	/* init this newx */
	cur = malloc(sizeof(xdbrequest_t));
	memset(cur, 0, sizeof(xdbrequest_t));
	// cur->set = 0;
	// cur->data = NULL;
	cur->ns = ns;
	cur->owner = owner;
	gettimeofday(&tv, NULL);
	cur->sent = tv.tv_sec;

	SEM_LOCK(xc->sem);
	cur->id = xc->id++;

	cur->next = xc->first;
	xc->first = cur;

	SEM_UNLOCK(xc->sem);

	/* send it on it's way */
	xdb_deliver(xc->i, cur, 0);
	cur->external = 1;

	/* if it hasn't already returned, we should block here until it returns */
	while (cur->preblock != 1)
		usleep(10);

	/* newx.data is now the returned xml packet */

	/* return the xmlnode inside <xdb>...</xdb> */
	for (x = xmlnode_get_firstchild(cur->data);
	     x != NULL && xmlnode_get_type(x) != NTYPE_TAG;
	     x = xmlnode_get_nextsibling(x));

	/* there were no children (results) to the xdb request, free the packet */
	if (x == NULL)
		xmlnode_free(cur->data);

	free(cur);
	return x;
}

/* sends new xml xdb action, data is NOT freed, app responsible for freeing it */
/* act must be NULL or "insert" for now, insert will either blindly insert data into the parent (creating one if needed) or use match */
/* match will find a child in the parent, and either replace (if it's an insert) or remove (if data is NULL) */
int xdb_act(xdbcache xc, jid owner, char *ns, char *act, char *match,
	    xmlnode data)
{
	xdbrequest_p cur;
	struct timeval tv;

	if (xc->shutdown)
		return 1;

	if (xc == NULL || owner == NULL || ns == NULL) {
		log_alert("xdb_set",
			  "Programming Error: xdb_set() called with NULL\n");
		return 1;
	}

	log_debug("XDB SET");


	cur = malloc(sizeof(xdbrequest_t));
	memset(cur, 0, sizeof(xdbrequest_t));

	cur->set = 1;
	cur->data = data;
	cur->ns = ns;
	cur->owner = owner;
	cur->act = act;
	cur->match = match;
	gettimeofday(&tv, NULL);
	cur->sent = tv.tv_sec;

	SEM_LOCK(xc->sem);
	cur->id = xc->id++;

	cur->next = xc->first;
	xc->first = cur;

	SEM_UNLOCK(xc->sem);

	/* send it on it's way */
	xdb_deliver(xc->i, cur, 0);
	cur->external = 1;

	/* if it hasn't already returned, we should block here until it returns */
	while (cur->preblock != 1)
		usleep(10);

	/* if it didn't actually get set, flag that */
	if (cur->data == NULL) {
		free(cur);
		return 1;
	}

	xmlnode_free(cur->data);

	free(cur);
	return 0;
}

/* sends new xml to replace old, data is NOT freed, app responsible for freeing it */
int xdb_set(xdbcache xc, jid owner, char *ns, xmlnode data)
{
	return xdb_act(xc, owner, ns, NULL, NULL, data);
}
