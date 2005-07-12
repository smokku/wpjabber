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
static short check_attr_value(xmlnode node, char *attr_name,
			      char *attr_value);
static int module_call(XdbSqlDatas * self, XdbSqlModule * mod, dpacket p,
		       char *user);
static void xdb_sql_shutdown(void *arg);
static result handle_unknown_namespace(dpacket p, const char *user,
				       const char *namespace);
static int handle_query_v2(XdbSqlDatas * self, query_def qd, dpacket p,
			   char *user);

static int initialized;

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

	/* Load config from xdb */
	xc = xdb_cache(i);
	self->config =
	    xdb_get(xc, jid_new(xmlnode_pool(x), "config@-internal"),
		    "jabberd:xdb_sql:config");

	/* Parse config */
	if (!xdbsql_config_init(self, self->config)) {	/* whoops! configuration failed! */
		log_error(NULL, "[xdb_sql] configuration failed");
		register_phandler(i, o_DELIVER, xdb_sql_phandler, self);
		register_shutdown(xdb_sql_shutdown, (void *) self);
		initialized = 0;
		return;
	}
	/* end if */
	initialized = 1;

	register_phandler(i, o_DELIVER, xdb_sql_phandler, self);
	register_shutdown(xdb_sql_shutdown, (void *) self);
}				/* end xdb_sql */

static void xdb_sql_shutdown(void *arg)
{
	XdbSqlDatas *self = (XdbSqlDatas *) arg;
	xdbsql_config_uninit(self);
}

/*
 * Handle xdb packets from jabberd.
 */
static result xdb_sql_phandler(instance i, dpacket p, void *args)
{
	char *user;
	char *namespace;
	const char *type;
	XdbSqlDatas *self = (XdbSqlDatas *) args;
	XdbSqlModule *mod;
	query_node qn;

	if (!initialized) {
		log_error(p->id->server,
			  "[xdb_sql] there was an error at setup");
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

	log_debug("modules = %p", self->modules);

	/* find module to dispatch according to resource/namespace */
	for (mod = self->modules; mod->namespace != NULL; mod++) {
		if (strcmp(namespace, mod->namespace) == 0) {
			SEM_LOCK(mod->sem);
			return module_call(self, mod, p, user);
			SEM_UNLOCK(mod->sem);
		}
	}

	for (qn = self->queries_v2; qn != NULL; qn = qn->next) {
		if ((strcmp(namespace, xdbsql_querydef_get_namespace(qn->qd)) == 0)
		    && (strcmp(type, xdbsql_querydef_get_type(qn->qd)) == 0)) {
			xdbsql_querydef_lock(qn->qd);
			return handle_query_v2(self, qn->qd, p, user);
			xdbsql_querydef_unlock(qn->qd);
		}
	}

	return handle_unknown_namespace(p, user, namespace);
}

static int
module_call(XdbSqlDatas * self, XdbSqlModule * mod, dpacket p, char *user)
{
	short is_set;
	xmlnode data = NULL;
	dpacket dp;

	is_set = check_attr_value(p->x, "type", "set");

	if (is_set) {
		/*
		 * The xml node is surrounded by an xdb element. Get rid of it,
		 * by taking the first child.
		 */
		xmlnode child = xmlnode_get_firstchild(p->x);

		/* HACK */
		if (mod->set == xdbsql_offline_set) {
			/* send the full xdb node */
			if ((mod->set) (self, user, p->x) == 0)
				return r_ERR;
		} else if ((mod->set) (self, user, child) == 0)
			return r_ERR;
	} else {
		data = (mod->get) (self, user);
		if (data) {
			xmlnode_insert_tag_node(p->x, data);
			xmlnode_free(data);
		}
	}

	/* reply */
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

static result
handle_unknown_namespace(dpacket p, const char *user,
			 const char *namespace)
{
	dpacket dp;

	/* special case of handling for unknown namespaces:
	 * if it's a "set" and xmlnode is null, process it
	 */
	if (check_attr_value(p->x, "type", "set")
	    && (xmlnode_get_firstchild(p->x) == NULL)) {
		xmlnode_put_attrib(p->x, "type", "result");
		xmlnode_put_attrib(p->x, "to",
				   xmlnode_get_attrib(p->x, "from"));
		xmlnode_put_attrib(p->x, "from", jid_full(p->id));

		dp = dpacket_new(p->x);
		deliver(dp, NULL);
		return r_DONE;
	}

	/*
	 * Deliver a dummy packet and return r_DONE if namespace not handled
	 * to not trigger the jabberd bug (free udata before reusing it
	 * as the xdb request is reissued later, see jdev post from 15-nov-2000)
	 */

/*  #define HIDE_BUG 1 */

#ifdef HIDE_BUG
	{
		xmlnode_put_attrib(p->x, "type", "result");
		xmlnode_put_attrib(p->x, "to",
				   xmlnode_get_attrib(p->x, "from"));
		xmlnode_put_attrib(p->x, "from", jid_full(p->id));

		dp = dpacket_new(p->x);
		deliver(dp, NULL);
		return r_DONE;
	}
#else				/* ! HIDE_BUG */
	return r_PASS;
#endif				/* HIDE_BUG */
}

static int
handle_query_v2(XdbSqlDatas * self, query_def qd, dpacket p, char *user)
{
	xmlnode data = NULL;
	dpacket dp;

	if (!strcmp(xdbsql_querydef_get_type(qd), "set")) {
		/*
		 * The xml node is surrounded by an xdb element. Get rid of it,
		 * by taking the first child.
		 */
		xmlnode child = xmlnode_get_firstchild(p->x);

		if (!child || !j_strcmp(xmlnode_get_name(child), "xdb")) {
			log_error(NULL,
				  "[xdb_sql] unexpected child in node [%s]",
				  xmlnode2str(p->x));
			return r_ERR;
		}
		if (xdbsql_simple_set(self, qd, user, child) == 0)
			return r_ERR;
	} else {
		data = xdbsql_simple_get(self, qd, user);
		if (data) {
			xmlnode_insert_tag_node(p->x, data);
			xmlnode_free(data);
		}
	}

	/* reply */
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
