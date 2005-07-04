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

xmlnode xdbsql_offline_get(XdbSqlDatas * self, const char *user)
{
	xmlnode rc = NULL;	/* return from this function */
	xmlnode query;		/* the query for this function */
	xmlnode msg;		/* the message being constructed */
	query_def qd;		/* the query definition */
	XdbSqlResult *result;	/* pointer to database result */
	int first = 1;		/* first time through loop? */
	col_map map;		/* column map variable */
	const char *sptr;	/* string pointer */
	spool s;		/* string pooling */
	int ndx_to, ndx_from, ndx_id, ndx_priority, ndx_type, ndx_thread;
	int ndx_subject, ndx_body, ndx_ext;

	if (!user) {		/* the user was not specified - we have to bug off */
		log_error(ZONE, "user not specified");
		return NULL;

	}

	/* end if */
	/* Get the query definition. */
	query = xdbsql_query_get(self, "despool");
	if (!query) {		/* no query - eep! */
		log_error(NULL, "--!!-- WTF? despool query not found?");
		return NULL;

	}

	/* end if */
	/* Build the query string. */
	qd = xdbsql_querydef_init(self, query);
	xdbsql_querydef_setvar(qd, "user", user);

	/* Execute the query! */
	result = sqldb_query(self, xdbsql_querydef_finalize(qd));
	xdbsql_querydef_free(qd);
	if (!result) {		/* the query failed - bail out! */
		log_error(NULL, "[xdbsql_offline_get] query failed : %s",
			  sqldb_error(self));
		return NULL;

	}

	/* end if */
	/* get the results from the query */
	if (!sqldb_use_result(result)) {	/* unable to retrieve the result */
		log_error(NULL,
			  "[xdbsql_offline_get] result fetch failed : %s",
			  sqldb_error(self));
		return NULL;

	}

	/* end if */
	/* Initialize the return value. */
	rc = xmlnode_new_tag("foo");
	xmlnode_put_attrib(rc, "xmlns", NS_OFFLINE);

	while (sqldb_next_tuple(result)) {	/* look for all offline messages and add them to the list */
		if (first) {	/* figure out all the indexes for our columns */
			map = xdbsql_colmap_init(query);
			ndx_to = xdbsql_colmap_index(map, "to");
			ndx_from = xdbsql_colmap_index(map, "from");
			ndx_id = xdbsql_colmap_index(map, "id");
			ndx_priority =
			    xdbsql_colmap_index(map, "priority");
			ndx_type = xdbsql_colmap_index(map, "type");
			ndx_thread = xdbsql_colmap_index(map, "thread");
			ndx_subject = xdbsql_colmap_index(map, "subject");
			ndx_body = xdbsql_colmap_index(map, "body");
			ndx_ext = xdbsql_colmap_index(map, "x");
			xdbsql_colmap_free(map);
			first = 0;

		}

		/* end if */
		/* get the extensions and start building the message tree */
		sptr = sqldb_get_value(result, ndx_ext);
		if (sptr && *sptr) {
			xmlnode node;
			/* this is tricky because there may be multiple <x> tags; parse them with
			 * the <message></message> surounding them
			 */
			s = spool_new(xmlnode_pool(rc));
			spooler(s, "<message>", sptr, "</message>", s);
			sptr = spool_print(s);
			node = xmlnode_str((char *) sptr, strlen(sptr));
			msg = xmlnode_insert_tag_node(rc, node);
			xmlnode_free(node);
		} /* end if */
		else		/* just create a raw <message> tag */
			msg = xmlnode_insert_tag(rc, "message");

		/* figure out the <body> and add it to the tree */
		sptr = sqldb_get_value(result, ndx_body);
		if (sptr && *sptr) {
			xmlnode node;
			/* the body text may be XML too...encapsulate it in <body>
			   tag and link it in */
			s = spool_new(xmlnode_pool(rc));
			spooler(s, "<body>", sptr, "</body>", s);
			sptr = spool_print(s);
			node = xmlnode_str((char *) sptr, strlen(sptr));
			xmlnode_insert_node(msg, node);
			xmlnode_free(node);
		}

		/* end if */
		/* add additional attributes on the <message> tag */
		sptr = sqldb_get_value(result, ndx_to);
		if (sptr && *sptr)
			xmlnode_put_attrib(msg, "to", sptr);
		sptr = sqldb_get_value(result, ndx_from);
		if (sptr && *sptr)
			xmlnode_put_attrib(msg, "from", sptr);
		sptr = sqldb_get_value(result, ndx_id);
		if (sptr && *sptr)
			xmlnode_put_attrib(msg, "id", sptr);
		sptr = sqldb_get_value(result, ndx_type);
		if (sptr && *sptr)
			xmlnode_put_attrib(msg, "type", sptr);

		/* add additional subtags under <message> */
		sptr = sqldb_get_value(result, ndx_priority);
		if (sptr && *sptr)
			xmlnode_insert_cdata(xmlnode_insert_tag
					     (msg, "priority"), sptr, -1);
		sptr = sqldb_get_value(result, ndx_thread);
		if (sptr && *sptr)
			xmlnode_insert_cdata(xmlnode_insert_tag
					     (msg, "thread"), sptr, -1);
		sptr = sqldb_get_value(result, ndx_subject);
		if (sptr && *sptr)
			xmlnode_insert_cdata(xmlnode_insert_tag
					     (msg, "subject"), sptr, -1);

	}			/* end while */

	sqldb_free_result(result);
	return rc;

}				/* end xdbsql_offline_get */

static int purge_offline(XdbSqlDatas * self, const char *user)
{
	xmlnode query;		/* the query for this function */
	query_def qd;		/* the query definition */
	XdbSqlResult *result;	/* return code from query */

	/* Get the query definition. */
	query = xdbsql_query_get(self, "spool-remove");
	if (!query) {		/* no query - eep! */
		log_error(NULL,
			  "--!!-- WTF? spool-remove query not found?");
		return 0;

	}

	/* end if */
	/* Build the query string. */
	qd = xdbsql_querydef_init(self, query);
	xdbsql_querydef_setvar(qd, "user", user);

	/* Execute the query! */
	result = sqldb_query(self, xdbsql_querydef_finalize(qd));
	xdbsql_querydef_free(qd);
	if (!result) {		/* the database query failed */
		log_error(NULL, "[purge_offline] query failed");
		return 0;

	}			/* end if */
	sqldb_free_result(result);
	return 1;		/* Qa'pla! */

}				/* end purge_offline */

static int test_root_node(xmlnode root, const char *tag)
{
	log_debug("checking %s", xmlnode2str(root));
	if (j_strcmp(xmlnode_get_name(root), tag) != 0) {
		log_error(ZONE, "incorrect root node");
		return 0;

	}
	/* end if */
	return 1;		/* ship it */

}				/* end test_root_offline_node */


/* load a query with an offline message */
static int load_query(query_def qd, xmlnode x, pool p)
{
	xmlnode x2;
	char *sptr;		/* temporary string pointer */
	char *data_subject, *data_thread, *data_priority, *data_body,
	    *data_date;
	spool data_ext;		/* extension data string pool */

	/* Use the attributes to fill in query variables. */
	sptr = xmlnode_get_attrib(x, "from");
	if (sptr && *sptr)
		xdbsql_querydef_setvar(qd, "from", sptr);
	sptr = xmlnode_get_attrib(x, "id");
	if (sptr && *sptr)
		xdbsql_querydef_setvar(qd, "id", sptr);
	sptr = xmlnode_get_attrib(x, "type");
	if (sptr && *sptr)
		xdbsql_querydef_setvar(qd, "type", sptr);

	data_priority = 0;
	data_subject = data_thread = data_body = data_date = NULL;
	data_ext = spool_new(p);

	for (x2 = xmlnode_get_firstchild(x); x2; x2 = xmlnode_get_nextsibling(x2)) {	/* handle all the child nodes of the message */

		if (j_strcmp(xmlnode_get_name(x2), "priority") == 0)
			data_priority =
			    xmlnode_get_data(xmlnode_get_firstchild(x2));
		else if (j_strcmp(xmlnode_get_name(x2), "thread") == 0)
			data_thread =
			    xmlnode_get_data(xmlnode_get_firstchild(x2));
		else if (j_strcmp(xmlnode_get_name(x2), "subject") == 0)
			data_subject =
			    xmlnode_get_data(xmlnode_get_firstchild(x2));
		else if (j_strcmp(xmlnode_get_name(x2), "body") == 0) {	/* the body text requires special handling */
			spool s = spool_new(p);
			spool_add(s, xmlnode_get_data(x2));
			data_body = spool_print(s);

		} /* end else if */
		else if (j_strcmp(xmlnode_get_name(x2), "x") == 0) {
			spool_add(data_ext, xmlnode2str(x2));
			data_date = xmlnode_get_attrib(x2, "stamp");
		}
	}			/* end for */

	/* Fill in the rest of the query parameters. */
	if (data_priority && *data_priority)
		xdbsql_querydef_setvar(qd, "priority", data_priority);
	if (data_thread && *data_thread)
		xdbsql_querydef_setvar(qd, "thread", data_thread);
	if (data_subject && *data_subject)
		xdbsql_querydef_setvar(qd, "subject", data_subject);
	if (data_body && *data_body)
		xdbsql_querydef_setvar(qd, "body", data_body);
	xdbsql_querydef_setvar(qd, "x", spool_print(data_ext));
	if (data_date && *data_date)
		xdbsql_querydef_setvar(qd, "date", data_date);

	return 0;
}


int xdbsql_offline_set(XdbSqlDatas * self, const char *user,
		       xmlnode xdb_request_node)
{
	xmlnode x;
	xmlnode data;
	xmlnode query;		/* the query for this function */
	query_def qd;		/* the query definition */
	XdbSqlResult *result;	/* return from query */
	pool p;			/* memory pool for this operation */
	const char *querystring;
	const char *action;
	int insert_mode = 0;

	data = xmlnode_get_firstchild(xdb_request_node);

	if (!user) {		/* the user was not specified - we have to bug off */
		log_error(NULL, "[xdbsql_offline_set] user not specified");
		return 1;

	}
	/* end if */
	if (!data)
		return purge_offline(self, user);

	action = xmlnode_get_attrib(xdb_request_node, "action");
	if (action) {
		if (j_strcmp("insert", action) != 0) {
			log_error(ZONE,
				  "don't know how to handle action (%s)",
				  action);
			return 1;
		}
		insert_mode = 1;
	}

	/* Get the query definition. */
	query = xdbsql_query_get(self, "spool");
	if (!query) {
		log_error(NULL, "--!!-- WTF? spool query not found?");
		return 1;
	}

	p = pool_new();

	/* a simple xdb_set transports several messages grouped inside an
	   <offline> node, whereas an action='insert' command comes with a
	   single <message> node
	 */
	if (insert_mode) {
		if (!test_root_node(data, "message"))
			return 1;	/* error already reported */

		/* Start to build the query string. */
		qd = xdbsql_querydef_init(self, query);
		xdbsql_querydef_setvar(qd, "to", user);
		load_query(qd, data, p);

		/* Execute the query! */
		querystring = xdbsql_querydef_finalize(qd);
		result = sqldb_query(self, querystring);
		xdbsql_querydef_free(qd);
		if (!result)
			log_error(ZONE, "query failed : %s",
				  sqldb_error(self));
		else
			sqldb_free_result(result);
	} else {
		/* old set "all-or-nothing" behaviour */

		if (!test_root_node(data, "foo"))
			return 1;	/* error already reported */

		/* Purge any existing offline messages. */
		(void) purge_offline(self, user);

		for (x = xmlnode_get_firstchild(data); x;
		     x = xmlnode_get_nextsibling(x)) {
			if (!j_strcmp("message", xmlnode_get_name(x))) {
				continue;
			}

			/* Start to build the query string. */
			qd = xdbsql_querydef_init(self, query);
			xdbsql_querydef_setvar(qd, "to", user);
			load_query(qd, x, p);

			/* Execute the query! */
			querystring = xdbsql_querydef_finalize(qd);
			result = sqldb_query(self, querystring);
			xdbsql_querydef_free(qd);
			if (!result)
				log_error(ZONE, "query failed : %s",
					  sqldb_error(self));
			else
				sqldb_free_result(result);
		}
	}

	pool_free(p);

	return 1;

}				/* end xdbsql_offline_set */
