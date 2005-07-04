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

#define GET_CHILD_DATA(X) xmlnode_get_data(xmlnode_get_firstchild(X))

xmlnode xdbsql_register_get(XdbSqlDatas * self, const char *user)
{
	xmlnode query;		/* the actual query to be executed */
	xmlnode data = NULL;	/* return data from this function */
	xmlnode tmp = NULL;	/* used temporarly while building node */
	query_def qd;		/* query definition */
	XdbSqlResult *result;	/* pointer to database result */
	int first = 1;		/* is this the first runthrough? */
	col_map map;		/* column mapping */
	const char *sptr;	/* string pointer */
	int ndx_login, ndx_password, ndx_email, ndx_name, ndx_stamp,
	    ndx_type;

	if (!user) {		/* the user was not specified - we have to bug off */
		log_error(NULL,
			  "[xdbsql_register_get] user not specified");
		return NULL;

	}

	/* end if */
	/* Get the query we need to execute. */
	query = xdbsql_query_get(self, "register-get");
	if (!query) {		/* that query should have been specified... */
		log_error(NULL,
			  "--!!-- WTF? register-get query not found?");
		return NULL;

	}

	/* end if */
	/* Build the query. */
	qd = xdbsql_querydef_init(self, query);
	xdbsql_querydef_setvar(qd, "user", user);

	/* Execute the query! */
	result = sqldb_query(self, xdbsql_querydef_finalize(qd));
	xdbsql_querydef_free(qd);
	if (!result) {		/* the query failed - bye now! */
		log_error(NULL, "[xdbsql_register_get] query failed : %s",
			  sqldb_error(self));
		return NULL;

	}

	/* end if */
	/* get the results from the query */
	if (!sqldb_use_result(result)) {	/* the result fetch failed - bye now! */
		log_error(NULL,
			  "[xdbsql_register_get] result fetch failed : %s",
			  sqldb_error(self));
		return NULL;

	}

	/* end if */
	/* create the return xmlnode */
	data = xmlnode_new_tag("query");
	xmlnode_put_attrib(data, "xmlns", NS_REGISTER);

	while (sqldb_next_tuple(result)) {	/* look for all vcard vCards and add them to the list */
		if (first) {	/* figure out all the indexes for our columns */

			map = xdbsql_colmap_init(query);
			ndx_login = xdbsql_colmap_index(map, "login");
			ndx_password =
			    xdbsql_colmap_index(map, "password");
			ndx_email = xdbsql_colmap_index(map, "email");
			ndx_name = xdbsql_colmap_index(map, "name");
			ndx_stamp = xdbsql_colmap_index(map, "stamp");
			ndx_type = xdbsql_colmap_index(map, "type");

			xdbsql_colmap_free(map);
			first = 0;

		}
		/* end if */
		sptr = sqldb_get_value(result, ndx_login);
		if (sptr && *sptr)
			xmlnode_insert_cdata(xmlnode_insert_tag
					     (data, "username"), sptr, -1);

		sptr = sqldb_get_value(result, ndx_password);
		if (sptr && *sptr) {
			tmp = xmlnode_insert_tag(data, "password");
			xmlnode_insert_cdata(tmp, sptr, -1);
			xmlnode_put_attrib(tmp, "xmlns", NS_AUTH);
		}

		sptr = sqldb_get_value(result, ndx_email);
		if (sptr && *sptr)
			xmlnode_insert_cdata(xmlnode_insert_tag
					     (data, "email"), sptr, -1);

		sptr = sqldb_get_value(result, ndx_name);
		if (sptr && *sptr)
			xmlnode_insert_cdata(xmlnode_insert_tag
					     (data, "name"), sptr, -1);

		sptr = sqldb_get_value(result, ndx_stamp);
		if (sptr && *sptr) {
			tmp = xmlnode_insert_tag(data, "x");
			xmlnode_put_attrib(tmp, "xmlns", NS_DELAY);
			xmlnode_put_attrib(tmp, "stamp", sptr);
			sptr = sqldb_get_value(result, ndx_type);
			if (sptr && *sptr)
				xmlnode_insert_cdata(tmp, sptr, -1);
		}

	}			/* end while */

	sqldb_free_result(result);

	return data;

}				/* end xdbsql_register_get */

static int purge_register(XdbSqlDatas * self, const char *user)
{
	xmlnode query;		/* the actual query to be executed */
	query_def qd;		/* query definition */
	XdbSqlResult *result;	/* return from query */

	/* Get the query we need to execute. */
	query = xdbsql_query_get(self, "register-remove");
	if (!query) {		/* that query should have been specified... */
		log_error(NULL,
			  "--!!-- WTF? register-remove query not found?");
		return 0;

	}

	/* end if */
	/* Build the query. */
	qd = xdbsql_querydef_init(self, query);
	xdbsql_querydef_setvar(qd, "user", user);

	/* Execute the query! */
	result = sqldb_query(self, xdbsql_querydef_finalize(qd));
	xdbsql_querydef_free(qd);
	if (!result) {		/* the query failed - bye now! */
		log_error(NULL, "[purge_register] query failed : %s",
			  sqldb_error(self));
		return 0;

	}			/* end if */
	sqldb_free_result(result);

	return 1;		/* Qa'pla! */

}				/* end purge_register */

static int test_root_register_node(xmlnode root)
{
	if (j_strcmp(xmlnode_get_name(root), "query") != 0) {	/* it's not an IQ query node */
		log_error(NULL,
			  "[xdbsql_register_set] not a <query> node");
		return 0;

	}
	/* end if */
	if (j_strcmp(xmlnode_get_attrib(root, "xmlns"), NS_REGISTER) != 0) {	/* it's not got the right namespace */
		log_error(NULL,
			  "[xdbsql_register_set] not a jabber:iq:register node");
		return 0;

	}			/* end if */
	return 1;		/* ship it */

}				/* end test_root_register_node */

int xdbsql_register_set(XdbSqlDatas * self, const char *user, xmlnode data)
{
	xmlnode query;		/* the actual query to be executed */
	xmlnode tmp;		/* for cycling through data children */
	query_def qd;		/* query definition */
	XdbSqlResult *result;	/* return from query */
	char *data_login = NULL;
	char *data_password = NULL;
	char *data_email = NULL;
	char *data_name = NULL;
	char *data_stamp = NULL;
	char *data_type = NULL;
	char *name;

	if (!user) {		/* the user was not specified - we have to bug off */
		log_error(NULL,
			  "[xdbsql_register_set] user not specified");
		return 0;

	}
	/* end if */
	if (!data)		/* we're erasing the data */
		return purge_register(self, user);

	if (!test_root_register_node(data))
		return 0;	/* error already reported */

	/* Get the query we need to execute. */
	query = xdbsql_query_get(self, "register-set");
	if (!query) {		/* that query should have been specified... */
		log_error(NULL,
			  "--!!-- WTF? register-set query not found?");
		return 0;

	}

	/* end if */
	/* Remove all existing entries for a user */
	(void) purge_register(self, user);

	/* Start to build the query string. */
	qd = xdbsql_querydef_init(self, query);
	xdbsql_querydef_setvar(qd, "user", user);

	for (tmp = xmlnode_get_firstchild(data); tmp;
	     tmp = xmlnode_get_nextsibling(tmp)) {
		name = xmlnode_get_name(tmp);

		if (j_strcmp(name, "username") == 0)
			data_login = GET_CHILD_DATA(tmp);
		else if (j_strcmp(name, "password") == 0)
			data_password = GET_CHILD_DATA(tmp);
		else if (j_strcmp(name, "email") == 0)
			data_email = GET_CHILD_DATA(tmp);
		else if (j_strcmp(name, "name") == 0)
			data_name = GET_CHILD_DATA(tmp);
		else if (j_strcmp(name, "x") == 0) {
			data_stamp = xmlnode_get_attrib(tmp, "stamp");
			data_type = GET_CHILD_DATA(tmp);
		}
	}			/* end for */

	/* Fill in the rest of the query parameters. */
	if (data_login && *data_login)
		xdbsql_querydef_setvar(qd, "login", data_login);
	if (data_password && *data_password)
		xdbsql_querydef_setvar(qd, "password", data_password);
	if (data_email && *data_email)
		xdbsql_querydef_setvar(qd, "email", data_email);
	if (data_name && *data_name)
		xdbsql_querydef_setvar(qd, "name", data_name);
	if (data_stamp && *data_stamp)
		xdbsql_querydef_setvar(qd, "stamp", data_stamp);
	if (data_type && *data_type)
		xdbsql_querydef_setvar(qd, "type", data_type);

	/* Execute the query! */
	result = sqldb_query(self, xdbsql_querydef_finalize(qd));

	xdbsql_querydef_free(qd);
	if (!result)
		log_error(NULL,
			  "[xdbsql_register_set] query failed for %s : %s",
			  user, sqldb_error(self));
	else
		sqldb_free_result(result);

	return 1;		/* Qa'pla! */
}				/* end xdbsql_register_set */
