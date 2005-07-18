/* --------------------------------------------------------------------------
 *
 *  This program was developed by IDEALX (http://www.IDEALX.org), for
 *  LibertySurf Télécom (http://www.libertysurftelecom.fr), based on
 *  code developped by members of the Jabber Team in xdb/mysql.
 *
 *  Initial release in xdb_sql 1.0, (C) 2000 - 2001 LibertySurf
 *  Télécom
 *
 *  Individual authors' names can be found in the AUTHORS file
 *
 *  Usage, modification and distribution rights reserved. See file
 *  COPYING for details.
 *
 * --------------------------------------------------------------------------*/

#include "xdb_sql.h"
#include <assert.h>

static result xdb_sql_phandler(instance i, dpacket p, void *args);
static short check_attr_value(xmlnode node, char *attr_name, char *attr_value);
static int namespace_call(XdbSqlDatas * self, char *namespace, const char *type, volatile xmlnode *data, char *user);
static int module_call(XdbSqlDatas * self, XdbSqlModule * mod, volatile xmlnode *data, char *user, int is_set);
result xdb_sql_purge(void *arg);
static void xdb_sql_shutdown(void *arg);
static result handle_unknown_namespace(volatile xmlnode *data, const char *user, const char *namespace, int is_set);
static int handle_query_v2(XdbSqlDatas * self, query_def qd, volatile xmlnode *data, char *user, int is_set);

/*
 * Setup xdb_sql, register handler.
 */
void xdb_sql(instance i, xmlnode x)
{
	xdbcache xc = NULL;	/* for config request */
	XdbSqlDatas *self = NULL;

	/* setup internal xdb_sql structures */
	log_debug("loading xdb_sql");

	self = pmalloco(i->p, sizeof(*self));
	if (!self) {
		log_error(NULL, "[xdb_sql] memory allocation failed");
		return;
	}
	self->poolref = i->p;
	self->shutdown = 0;
	self->actualtime = time(NULL);
	self->last = NULL;	//last element in list, start removing from here
	self->first = NULL;	//first element in list

	/* Load config from xdb */
	xc = xdb_cache(i);
	self->config = xdb_get(xc, jid_new(xmlnode_pool(x), "config@-internal"), "jabberd:xdb_sql:config");

	/* Parse config */
	if (!xdbsql_config_init(self, self->config)) {
		/* whoops! configuration failed! */
		log_error(NULL, "[xdb_sql] configuration failed");
		register_phandler(i, o_DELIVER, xdb_sql_phandler, self);
		register_shutdown(xdb_sql_shutdown, (void *) self);
		self->initialized = 0;
		return;
	} /* end if */
	
	SEM_INIT(self->hash_sem);
	self->hash = wpxhash_new(j_atoi(xmlnode_get_tag_data(self->config, "maxcache"), CACHE_PRIME));
	
	self->initialized = 1;

	register_phandler(i, o_DELIVER, xdb_sql_phandler, self);
	register_shutdown(xdb_sql_shutdown, (void *) self);
	if (self->timeout > 0)
		/* 0 is expired immediately, -1 is cached forever */
		register_beat(10, xdb_sql_purge, (void *) self);
}	/* end xdb_sql */

inline void xdb_sql_cacher_dump(XdbSqlDatas *self, cacher c)
{
	char *namespace, *user, *skip;
	
	user = pstrdup(self->poolref, c->wpkey);
	skip = strchr(user, '/');
	if (skip != NULL) {
		*(skip++) = '\0';
		namespace = pstrdup(self->poolref, skip);
		skip = strchr(namespace, '@');
		if (skip != NULL)
			*skip = '\0';
		log_debug("xdb_sql dumping cached data %s/%s: %s", user, namespace, xmlnode2str(c->data));
		namespace_call(self, namespace, "set", &(c->data), user);
		/* don't check return value - errors are already reported
		   and we coldn't do much here though */
	}
}

static void xdb_sql_shutdown(void *arg)
{
	cacher c;
	
	XdbSqlDatas *self = (XdbSqlDatas *) arg;
	self->shutdown = 1;
	/* hash walk and dump all dirty elements */
	SEM_LOCK(self->hash_sem);
	for (c = self->last; c != NULL;) {
		/* dump to DB if needed */
		if (c->dirty)
			xdb_sql_cacher_dump(self, c);
		c = c->aprev;
	}
	SEM_UNLOCK(self->hash_sem);
	xdbsql_config_uninit(self);
}

inline void cacher_add_element(XdbSqlDatas * self, cacher c)
{
	/* first element */
	if (self->last == NULL)
		self->last = c;
	else
		self->first->aprev = c;

	c->anext = self->first;
	self->first = c;
}


inline void cacher_remove_element(XdbSqlDatas * self, cacher c)
{

	if (c->aprev) {
		c->aprev->anext = c->anext;
	}
	if (c->anext) {
		c->anext->aprev = c->aprev;
	}
	if (self->first == c) {
		self->first = c->anext;
	}
	if (self->last == c) {
		self->last = c->aprev;
	}
	c->anext = c->aprev = NULL;
	return;

}

inline void cacher_touch_element(XdbSqlDatas * self, cacher c)
{
	/* is it add */
	if ((c->aprev != NULL) || (c->anext != NULL) || (self->first == c)) {
		cacher_remove_element(self, c);
	}
	cacher_add_element(self, c);
}

/* remove expired elements */
result xdb_sql_purge(void *arg)
{
	XdbSqlDatas * self = (XdbSqlDatas *) arg;
	cacher c;
	cacher t;

	SEM_LOCK(self->hash_sem);
	self->actualtime = time(NULL);

	if ((self->last == NULL) || (self->shutdown)) {
		SEM_UNLOCK(self->hash_sem);
		return r_DONE;
	}

	t = NULL;

	/* loop till c and data are valid */
	for (c = self->last; c != NULL;) {

		/* if someone is using this */
		if (c->ref)
			break;
		if (c->lasttime > self->actualtime - self->timeout)
			break;

		/* no ref and old */

		t = c;

		c = t->aprev;

		t->aprev = NULL;
		t->anext = NULL;

		/* dump to DB if needed */
		if (t->dirty)
			xdb_sql_cacher_dump(self, t);

		/* remove */
		log_debug("xdb_sql removing cached data %s", t->wpkey);
		wpxhash_zap(self->hash, t->wpkey);

		/* free */
		xmlnode_free(t->data);
	}

	/* t elem is free , c is last one */

	/* if something was removed */
	if (t) {
		/* if last remove everything */
		if (c) {
			self->last = c;
			c->anext = NULL;
		} else {
			self->first = NULL;
			self->last = NULL;
		}
	}
	SEM_UNLOCK(self->hash_sem);
	return r_DONE;
}
/*
 * Handle xdb packets from jabberd.
 */
static result xdb_sql_phandler(instance i, dpacket p, void *args)
{
	char *user;
	char *namespace;
	const char *type, *action;
	XdbSqlDatas *self = (XdbSqlDatas *) args;
	short is_set;
	char *hashkey;
	cacher c;
	xmlnode data, t;
	dpacket dp;

	if (!self->initialized) {
		log_error(p->id->server,
			  "[xdb_sql] there was an error at setup");
		return r_ERR;
	}

	if (self->shutdown) {
		return r_ERR;
	}

	assert(p != NULL);
	assert(p->id != NULL);

	user = spools(p->p, p->id->user, "@", p->id->server, p->p);
	namespace = xmlnode_get_attrib(p->x, "ns");

	if (self->sleep_debug != 0) {
		sleep(self->sleep_debug);
	}

	if (user == NULL)
		return r_PASS;
	if (namespace == NULL)
		return r_PASS;

	if (p->x == NULL) {
		log_error(p->id->server,
			  "[xdb_sql] no xml node in dpacket %p", p);
		return r_ERR;
	}

	type = xmlnode_get_attrib(p->x, "type");
	if (type == NULL) {
		log_debug("boucing unknown packet");
		return r_ERR;
	}
	if (strncmp(type, "error", 5) == 0) {
		log_warn(ZONE, "dropping error packet");
		xmlnode_free(p->x);
		return r_DONE;
	}
	
	is_set = check_attr_value(p->x, "type", "set");

	/* The xml node is surrounded by an xdb element. Get rid of it,
	 * by taking the first child. */
	data = xmlnode_get_firstchild(p->x);
	
	/* HACK! move action= to the child subnode */
	action = xmlnode_get_attrib(p->x, "action");
	if (action != NULL) {
		xmlnode_hide_attrib(data, "action");
		xmlnode_put_attrib(data, "action", action);
	}
	
	/* need to differentiate action="insert" set's in the hash to not overwrite previous
	   so let's use the message-id */
	if(j_strcmp(action, "insert") == 0) {
		hashkey = spools(p->p, user, "/", namespace, "@", xmlnode_get_attrib(data, "id"), p->p);
	} else {
		hashkey = spools(p->p, user, "/", namespace, p->p);	
	}

	SEM_LOCK(self->hash_sem);
	if ((c = wpxhash_get(self->hash, hashkey)) != NULL) {
		c->ref++;
		log_debug("xdb_sql using cached data ref=%d for %s", c->ref, hashkey);
		/* wait for data, I don't like this :( */
		SEM_UNLOCK(self->hash_sem);
		while (1) {
			usleep(15);
			SEM_LOCK(self->hash_sem);
			if (c->data != NULL)
				break;
			SEM_UNLOCK(self->hash_sem);
		}
	} else {
		pool p = pool_heap(1024);
		c = pmalloco(p, sizeof(_cacher));
		c->ref = 1;
		wpxhash_put(self->hash, pstrdup(p, hashkey), c);
		log_debug("xqb_sql new cache for %s", hashkey);
	}

	if (is_set) {
		t = c->data;
		c->data = xmlnode_dup(data);
		xmlnode_free(t);
		/* mark need to dump to DB */
		c->dirty = 1;
	} else {
		if (c->data == NULL) {
			xmlnode x;
			int ret;
			
			SEM_UNLOCK(self->hash_sem);
			if ((ret = namespace_call(self, namespace, type, &x, user)) != r_DONE)
				return ret;
			if (x == NULL)
				/* put dummy node if null query-result */
				c->data = xmlnode_new_tag("xdb");
			else {
				c->data = xmlnode_dup(x);
				xmlnode_free(x);
			}
			SEM_LOCK(self->hash_sem);
		}
	}

	log_debug("xdb_sql %s/%s is: %s", hashkey, type, xmlnode2str(c->data));

	/* our ref-- */
	c->ref--;

	/* put data to result */
	/* if not set request and not dummy node */
	if (!is_set && j_strcmp(xmlnode2str(c->data), "<xdb/>"))
		xmlnode_insert_tag_node(p->x, c->data);

	if ((self->timeout == 0) && (c->ref == 0)) {
		/* if no cache and nobody waits for data - remove from hash */
		xmlnode_free(c->data);
		wpxhash_zap(self->hash, c->wpkey);
	} else if (self->timeout > 0) {
		/* if cache - insert to cacher list */
			c->lasttime = self->actualtime;
			cacher_touch_element(self, c);
	}

	SEM_UNLOCK(self->hash_sem);
		
	/* and reply */
	xmlnode_put_attrib(p->x, "type", "result");
	xmlnode_put_attrib(p->x, "to", xmlnode_get_attrib(p->x, "from"));
	xmlnode_put_attrib(p->x, "from", jid_full(p->id));

	dp = dpacket_new(p->x);
	if (dp) {
		deliver(dp, NULL);
		return r_DONE;
	} else
		return r_ERR;
}

static int namespace_call(XdbSqlDatas * self, char *namespace, const char *type, volatile xmlnode *data, char *user)
{
	XdbSqlModule *mod;
	query_node qn;
	int is_set;

	is_set = !j_strcmp(type, "set");
	
	/* find module to dispatch according to resource/namespace */
	for (mod = self->modules; mod->namespace != NULL; mod++) {
		if (strcmp(namespace, mod->namespace) == 0) {
			return module_call(self, mod, data, user, is_set);
		}
	}

	for (qn = self->queries_v2; qn != NULL; qn = qn->next) {
		if ((strcmp(namespace, xdbsql_querydef_get_namespace(qn->qd)) == 0)
		    && (strcmp(type, xdbsql_querydef_get_type(qn->qd)) == 0)) {
			return handle_query_v2(self, qn->qd, data, user, is_set);
		}
	}

	return handle_unknown_namespace(data, user, namespace, is_set);
}

static int module_call(XdbSqlDatas * self, XdbSqlModule * mod, volatile xmlnode *data, char *user, int is_set)
{
	log_debug("xdb_sql module = %p", mod);

	if (is_set) {
		if ((mod->set) (self, user, *data) == 0)
			return r_ERR;
	} else {
		*data = (mod->get) (self, user);
	}

	return r_DONE;
}

static short check_attr_value(xmlnode node, char *attr_name, char *value)
{
	char *attr_value;

	if (node == NULL)
		return 0;
	if (attr_name == NULL)
		return 0;
	if (value == NULL)
		return 0;

	attr_value = xmlnode_get_attrib(node, attr_name);
	if (strcmp(attr_value, value) == 0)
		return 1;
	else
		return 0;
}

static result handle_unknown_namespace(volatile xmlnode *data, const char *user, const char *namespace, int is_set)
{
	dpacket dp;

	/* special case of handling for unknown namespaces:
	 * if it's a "set" and xmlnode is null, process it
	 */
	if (is_set && *data == NULL)
		return r_DONE;

/*  #define HIDE_BUG 1 */

#ifdef HIDE_BUG
	/*
	 * Deliver a dummy packet (return r_DONE) if namespace not handled
	 * to not trigger the jabberd bug (free udata before reusing it
	 * as the xdb request is reissued later, see jdev post from 15-nov-2000)
	 */

	return r_DONE;
#else				/* ! HIDE_BUG */
	return r_PASS;
#endif				/* HIDE_BUG */
}

static int handle_query_v2(XdbSqlDatas * self, query_def qd, volatile xmlnode *data, char *user, int is_set)
{
	if (is_set) {
		if (!*data || !j_strcmp(xmlnode_get_name(*data), "xdb")) {
			log_error(NULL,
				  "[xdb_sql] unexpected node [%s]",
				  xmlnode2str(*data));
			return r_ERR;
		}
		if (xdbsql_simple_set(self, qd, user, *data) == 0)
			return r_ERR;
	} else {
		*data = xdbsql_simple_get(self, qd, user);
	}
	return r_DONE;
}
