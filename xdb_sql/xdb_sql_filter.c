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

xmlnode xdbsql_filter_get(XdbSqlDatas * self, const char *user)
{
	xmlnode rc = NULL;	/* return from this function */
	xmlnode query;		/* the query for this function */
	xmlnode rule;		/* the rule being constructed */
	query_def qd;		/* the query definition */
	XdbSqlResult *result;	/* pointer to database result */
	int first = 1;		/* first time through loop? */
	col_map map;		/* column map variable */
	const char *sptr;	/* string pointer */
	int ndx_unavailable;
	int ndx_from;
	int ndx_resource;
	int ndx_subject;
	int ndx_body;
	int ndx_show;
	int ndx_type;
	int ndx_offline;
	int ndx_forward;
	int ndx_reply;
	int ndx_continue;
	int ndx_settype;


	if (!user) {		/* the user was not specified - we have to bug off */
		log_error(NULL, "[xdbsql_filter_get] user not specified");
		return NULL;

	}

	/* end if */
	/* Get the query definition. */
	query = xdbsql_query_get(self, "filter-get");
	if (!query) {		/* no query - eep! */
		log_error(NULL, "--!!-- WTF? filter-get query not found?");
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
		log_error(NULL, "[xdbsql_filter_get] query failed : %s",
			  sqldb_error(self));
		return NULL;

	}

	/* end if */
	/* get the results from the query */
	if (!sqldb_use_result(result)) {	/* unable to retrieve the result */
		log_error(NULL,
			  "[xdbsql_filter_get] result fetch failed : %s",
			  sqldb_error(self));
		return NULL;

	}

	/* end if */
	/* Initialize the return value. */
	rc = xmlnode_new_tag("query");
	xmlnode_put_attrib(rc, "xmlns", NS_FILTER);

	/* add default rule */
	/* rule = xmlnode_insert_tag(rc,"rule");
	   xmlnode_insert_tag(rule, "unavailable");
	   xmlnode_insert_cdata(xmlnode_insert_tag(rule, "reply"),
	   "MESSAGE OFFLINE", -1);
	   xmlnode_insert_tag(rule, "offline");
	 */

	while (sqldb_next_tuple(result)) {	/* look for all filter rules and add them to the list */
		if (first) {	/* figure out all the indexes for our columns */
			map = xdbsql_colmap_init(query);
			ndx_unavailable =
			    xdbsql_colmap_index(map, "unavailable");
			ndx_from = xdbsql_colmap_index(map, "from");
			ndx_resource =
			    xdbsql_colmap_index(map, "resource");
			ndx_subject = xdbsql_colmap_index(map, "subject");
			ndx_body = xdbsql_colmap_index(map, "body");
			ndx_show = xdbsql_colmap_index(map, "show");
			ndx_type = xdbsql_colmap_index(map, "type");
			ndx_offline = xdbsql_colmap_index(map, "offline");
			ndx_forward = xdbsql_colmap_index(map, "forward");
			ndx_reply = xdbsql_colmap_index(map, "reply");
			ndx_continue =
			    xdbsql_colmap_index(map, "continue");
			ndx_settype = xdbsql_colmap_index(map, "settype");

			xdbsql_colmap_free(map);
			first = 0;

		}
		/* end if */
		rule = xmlnode_insert_tag(rc, "rule");

		sptr = sqldb_get_value(result, ndx_unavailable);
		if (sptr && *sptr)
			xmlnode_insert_tag(rule, "unavailable");

		sptr = sqldb_get_value(result, ndx_from);
		if (sptr && *sptr)
			xmlnode_insert_cdata(xmlnode_insert_tag
					     (rule, "from"), sptr, -1);

		sptr = sqldb_get_value(result, ndx_resource);
		if (sptr && *sptr)
			xmlnode_insert_cdata(xmlnode_insert_tag
					     (rule, "resource"), sptr, -1);

		sptr = sqldb_get_value(result, ndx_subject);
		if (sptr && *sptr)
			xmlnode_insert_cdata(xmlnode_insert_tag
					     (rule, "subject"), sptr, -1);

		sptr = sqldb_get_value(result, ndx_body);
		if (sptr && *sptr)
			xmlnode_insert_cdata(xmlnode_insert_tag
					     (rule, "body"), sptr, -1);

		sptr = sqldb_get_value(result, ndx_show);
		if (sptr && *sptr)
			xmlnode_insert_cdata(xmlnode_insert_tag
					     (rule, "show"), sptr, -1);

		sptr = sqldb_get_value(result, ndx_type);
		if (sptr && *sptr)
			xmlnode_insert_cdata(xmlnode_insert_tag
					     (rule, "type"), sptr, -1);

		sptr = sqldb_get_value(result, ndx_offline);
		if (sptr && *sptr)
			xmlnode_insert_tag(rule, "offline");

		sptr = sqldb_get_value(result, ndx_forward);
		if (sptr && *sptr)
			xmlnode_insert_cdata(xmlnode_insert_tag
					     (rule, "forward"), sptr, -1);

		sptr = sqldb_get_value(result, ndx_reply);
		if (sptr && *sptr)
			xmlnode_insert_cdata(xmlnode_insert_tag
					     (rule, "reply"), sptr, -1);

		sptr = sqldb_get_value(result, ndx_continue);
		if (sptr && *sptr)
			xmlnode_insert_tag(rule, "continue");

		sptr = sqldb_get_value(result, ndx_settype);
		if (sptr && *sptr)
			xmlnode_insert_cdata(xmlnode_insert_tag
					     (rule, "settype"), sptr, -1);

	}			/* end while */

	sqldb_free_result(result);
	return rc;

}				/* end xdbsql_filter_get */

static int purge_filter(XdbSqlDatas * self, const char *user)
{
	xmlnode query;		/* the query for this function */
	query_def qd;		/* the query definition */
	XdbSqlResult *result;	/* return code from query */

	/* Get the query definition. */
	query = xdbsql_query_get(self, "filter-remove");
	if (!query) {		/* no query - eep! */
		log_error(NULL,
			  "--!!-- WTF? filter-remove query not found?");
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
		log_error(NULL, "[purge_filter] query failed");
		return 0;

	}			/* end if */
	sqldb_free_result(result);
	return 1;		/* Qa'pla! */

}				/* end purge_filter */

static int test_root_filter_node(xmlnode root)
{
	if (j_strcmp(xmlnode_get_name(root), "query") != 0) {	/* it's not an IQ query node */
		log_error(NULL, "[xdbsql_filter_set] not a <query> node");
		return 0;

	}
	/* end if */
	if (j_strcmp(xmlnode_get_attrib(root, "xmlns"), NS_FILTER) != 0) {	/* it's not got the right namespace */
		log_error(NULL,
			  "[xdbsql_filter_set] not a jabber:iq:filter node");
		return 0;

	}
	/* end if */
	return 1;		/* ship it */

}				/* end test_root_filter_node */

int xdbsql_filter_set(XdbSqlDatas * self, const char *user, xmlnode data)
{
	xmlnode query;		/* the query for this function */
	xmlnode x, x2;		/* used to iterate through the rules */
	query_def qd;		/* the query definition */
	XdbSqlResult *result;	/* return from query */
	char *data_unavailable = NULL;
	char *data_from = NULL;
	char *data_resource = NULL;
	char *data_subject = NULL;
	char *data_body = NULL;
	char *data_show = NULL;
	char *data_type = NULL;
	char *data_offline = NULL;
	char *data_forward = NULL;
	char *data_reply = NULL;
	char *data_continue = NULL;
	char *data_settype = NULL;

	if (!user) {		/* the user was not specified - we have to bug off */
		log_error(NULL, "[xdbsql_filter_set] user not specified");
		return 0;

	}
	/* end if */
	if (!data)
		return purge_filter(self, user);

	if (!test_root_filter_node(data))
		return 0;	/* error already reported */

	/* Get the query definition. */
	query = xdbsql_query_get(self, "filter-set");
	if (!query) {		/* no query - eep! */
		log_error(NULL, "--!!-- WTF? filter-set query not found?");
		return 0;

	}

	/* end if */
	/* Purge any existing filter messages. */
	(void) purge_filter(self, user);

	for (x = xmlnode_get_firstchild(data); x; x = xmlnode_get_nextsibling(x)) {	/* Start to build the query string. */
		const char *querystring;

		if (j_strcmp(xmlnode_get_name(x), "rule"))
			continue;

		qd = xdbsql_querydef_init(self, query);
		xdbsql_querydef_setvar(qd, "user", user);

		for (x2 = xmlnode_get_firstchild(x); x2; x2 = xmlnode_get_nextsibling(x2)) {	/* handle all the child nodes of the message */

			if (j_strcmp(xmlnode_get_name(x2), "unavailable")
			    == 0)
				data_unavailable = "-";
			else if (j_strcmp(xmlnode_get_name(x2), "from") ==
				 0)
				data_from =
				    xmlnode_get_data(xmlnode_get_firstchild
						     (x2));
			else if (j_strcmp(xmlnode_get_name(x2), "resource")
				 == 0)
				data_resource =
				    xmlnode_get_data(xmlnode_get_firstchild
						     (x2));
			else if (j_strcmp(xmlnode_get_name(x2), "subject")
				 == 0)
				data_subject =
				    xmlnode_get_data(xmlnode_get_firstchild
						     (x2));
			else if (j_strcmp(xmlnode_get_name(x2), "body") ==
				 0)
				data_body =
				    xmlnode_get_data(xmlnode_get_firstchild
						     (x2));
			else if (j_strcmp(xmlnode_get_name(x2), "show") ==
				 0)
				data_show =
				    xmlnode_get_data(xmlnode_get_firstchild
						     (x2));
			else if (j_strcmp(xmlnode_get_name(x2), "type") ==
				 0)
				data_type =
				    xmlnode_get_data(xmlnode_get_firstchild
						     (x2));
			else if (j_strcmp(xmlnode_get_name(x2), "offline")
				 == 0)
				data_offline = "-";
			else if (j_strcmp(xmlnode_get_name(x2), "forward")
				 == 0)
				data_forward =
				    xmlnode_get_data(xmlnode_get_firstchild
						     (x2));
			else if (j_strcmp(xmlnode_get_name(x2), "reply") ==
				 0)
				data_reply =
				    xmlnode_get_data(xmlnode_get_firstchild
						     (x2));
			else if (j_strcmp(xmlnode_get_name(x2), "continue")
				 == 0)
				data_continue = "-";
			else if (j_strcmp(xmlnode_get_name(x2), "settype")
				 == 0)
				data_settype =
				    xmlnode_get_data(xmlnode_get_firstchild
						     (x2));
		}		/* end for */

		/* Fill in the rest of the query parameters. */
		if (data_unavailable && *data_unavailable)
			xdbsql_querydef_setvar(qd, "unavailable",
					       data_unavailable);
		if (data_from && *data_from)
			xdbsql_querydef_setvar(qd, "from", data_from);
		if (data_resource && *data_resource)
			xdbsql_querydef_setvar(qd, "resource",
					       data_resource);
		if (data_subject && *data_subject)
			xdbsql_querydef_setvar(qd, "subject",
					       data_subject);
		if (data_body && *data_body)
			xdbsql_querydef_setvar(qd, "body", data_body);
		if (data_show && *data_show)
			xdbsql_querydef_setvar(qd, "show", data_show);
		if (data_type && *data_type)
			xdbsql_querydef_setvar(qd, "type", data_type);
		if (data_offline && *data_offline)
			xdbsql_querydef_setvar(qd, "offline",
					       data_offline);
		if (data_forward && *data_forward)
			xdbsql_querydef_setvar(qd, "forward",
					       data_forward);
		if (data_reply && *data_reply)
			xdbsql_querydef_setvar(qd, "reply", data_reply);
		if (data_continue && *data_continue)
			xdbsql_querydef_setvar(qd, "continue",
					       data_continue);
		if (data_settype && *data_settype)
			xdbsql_querydef_setvar(qd, "settype",
					       data_settype);

		/* Execute the query! */
		querystring = xdbsql_querydef_finalize(qd);

		result = sqldb_query(self, querystring);

		xdbsql_querydef_free(qd);
		if (!result)
			log_error(NULL,
				  "[xdbsql_filter_set] query failed : %s",
				  sqldb_error(self));
		else
			sqldb_free_result(result);
	}			/* end for */

	return 1;		/* Qa'pla! */

}				/* end xdbsql_filter_set */
