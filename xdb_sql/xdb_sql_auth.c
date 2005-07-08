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


static int xdbsql_user_exists(XdbSqlDatas * self, const char *user);
static int xdbsql_userhash_exists(XdbSqlDatas * self, const char *user);
static char *extract_password(xmlnode data);
static char *extract_passwordhash(xmlnode data);

xmlnode xdbsql_auth_get(XdbSqlDatas * self, const char *user)
{
	xmlnode query;		/* the actual query to be executed */
	xmlnode data = NULL;	/* return data from this function */
	query_def qd;		/* query definition */
	XdbSqlResult *result;	/* pointer to database result */
#ifdef NUM_TUPLES
	int rowcount;		/* number of rows in result */
#endif
	short row;		/* true if there is a row available */
	col_map map;		/* column mapping */
	int ndx_password;	/* index of the password element */
	const char *querystr;	/* SQL query */

	if (!user) {		/* the user was not specified - we have to bug off */
		log_error(NULL, "[xdbsql_auth_get] user not specified");
		return NULL;

	}

	/* end if */
	/* Get the query we need to execute. */
	query = xdbsql_query_get(self, "auth-get");

	if (!query) {		/* that query should have been specified... */
		log_error(NULL, "--!!-- WTF? auth-get query not found?");
		return NULL;

	}

	/* end if */
	/* Build the query. */
	qd = xdbsql_querydef_init(self, query);
	xdbsql_querydef_setvar(qd, "user", user);

	/* Execute the query! */
	querystr = xdbsql_querydef_finalize(qd);
	result = sqldb_query(self, querystr);
	xdbsql_querydef_free(qd);
	if (!result) {		/* the query failed - bye now! */
		log_error(NULL, "[xdbsql_auth_get] query failed : %s",
			  sqldb_error(self));
		return NULL;

	}

	/* end if */
	/* get the results from the query */
	if (!sqldb_store_result(result)) {	/* the result fetch failed - bye now! */
		log_error(NULL,
			  "[xdbsql_auth_get] result fetch failed : %s",
			  sqldb_error(self));
		return NULL;

	}
	/* end if */
#ifdef NUM_TUPLES
	/* make sure we got back just one row full of info */
	rowcount = sqldb_num_tuples(result);
	if (rowcount == 1) {	/* now fetch a row full of data back from the database */
		row = sqldb_next_tuple(result);
		if (row) {	/* use a column map to get the index of the "password" field */
			map = xdbsql_colmap_init(query);
			ndx_password =
			    xdbsql_colmap_index(map, "password");
			xdbsql_colmap_free(map);

			/* build the result set */
			data = xmlnode_new_tag("password");
			xmlnode_insert_cdata(data,
					     sqldb_get_value(result,
							     ndx_password),
					     -1);

		} /* end if */
		else
			log_error(NULL,
				  "[xdbsql_auth_get] row fetch failed");

	} /* end if */
	else {			/* we didn't get back the exact row count we wanted */
		if (rowcount == 0)
			log_error(NULL,
				  "[xdbsql_auth_get] no results, user %s not found", user);
		else
			log_error(NULL,
				  "[xdbsql_auth_get] user %s not unique", user);

	}			/* end else */
#else
	row = sqldb_next_tuple(result);
	if (row) {		/* use a column map to get the index of the "password" field */
		map = xdbsql_colmap_init(query);
		ndx_password = xdbsql_colmap_index(map, "password");
		xdbsql_colmap_free(map);

		/* build the result set */
		data = xmlnode_new_tag("password");
		xmlnode_insert_cdata(data,
				     sqldb_get_value(result, ndx_password),
				     -1);

		/* Check for user unicity */
		row = sqldb_next_tuple(result);
		if (row) {	/* User is not unique... */
			log_error(NULL,
				  "[xdbsql_auth_get] user %s not unique", user);
			free(data);
			data = NULL;
		}
	} /* end if */
	else
		log_error(NULL,
			  "[xdbsql_auth_get] no results, user %s not found", user);
#endif

	sqldb_free_result(result);

	return data;

}				/* end xdbsql_auth_get */

xmlnode xdbsql_authhash_get(XdbSqlDatas * self, const char *user)
{
	xmlnode query;		/* the actual query to be executed */
	xmlnode data = NULL;	/* return data from this function */
	query_def qd;		/* query definition */
	XdbSqlResult *result;	/* pointer to database result */
#ifdef NUM_TUPLES
	int rowcount;		/* number of rows in result */
#endif
	short row;		/* true if there is a row available */
	col_map map;		/* column mapping */
	int ndx_password;	/* index of the password element */
	const char *querystr;	/* SQL query */

	if (!user) {		/* the user was not specified - we have to bug off */
		log_error(NULL,
			  "[xdbsql_authhash_get] user not specified");
		return NULL;

	}

	/* end if */
	/* Get the query we need to execute. */
	query = xdbsql_query_get(self, "authhash-get");

	if (!query) {		/* that query should have been specified... */
		log_error(NULL,
			  "--!!-- WTF? authhash-get query not found?");
		return NULL;

	}

	/* end if */
	/* Build the query. */
	qd = xdbsql_querydef_init(self, query);
	xdbsql_querydef_setvar(qd, "user", user);

	/* Execute the query! */
	querystr = xdbsql_querydef_finalize(qd);
	result = sqldb_query(self, querystr);
	xdbsql_querydef_free(qd);
	if (!result) {		/* the query failed - bye now! */
		log_error(NULL, "[xdbsql_authhash_get] query failed : %s",
			  sqldb_error(self));
		return NULL;

	}

	/* end if */
	/* get the results from the query */
	if (!sqldb_store_result(result)) {	/* the result fetch failed - bye now! */
		log_error(NULL,
			  "[xdbsql_authhash_get] result fetch failed : %s",
			  sqldb_error(self));
		return NULL;

	}
	/* end if */
#ifdef NUM_TUPLES
	/* make sure we got back just one row full of info */
	rowcount = sqldb_num_tuples(result);
	if (rowcount == 1) {	/* now fetch a row full of data back from the database */
		row = sqldb_next_tuple(result);
		if (row) {	/* use a column map to get the index of the "password" field */
			map = xdbsql_colmap_init(query);
			ndx_password =
			    xdbsql_colmap_index(map, "password");
			xdbsql_colmap_free(map);

			/* build the result set */
			data = xmlnode_new_tag("crypt");
			xmlnode_insert_cdata(data,
					     sqldb_get_value(result,
							     ndx_password),
					     -1);

		} /* end if */
		else
			log_error(NULL,
				  "[xdbsql_authhash_get] row fetch failed");

	} /* end if */
	else {			/* we didn't get back the exact row count we wanted */
		if (rowcount == 0)
			log_error(NULL,
				  "[xdbsql_authhash_get] no results, user %s not found", user);
		else
			log_error(NULL,
				  "[xdbsql_authhash_get] user %s not unique", user);

	}			/* end else */
#else
	row = sqldb_next_tuple(result);
	if (row) {		/* use a column map to get the index of the "password" field */
		map = xdbsql_colmap_init(query);
		ndx_password = xdbsql_colmap_index(map, "password");
		xdbsql_colmap_free(map);

		/* build the result set */
		data = xmlnode_new_tag("crypt");
		xmlnode_insert_cdata(data,
				     sqldb_get_value(result, ndx_password),
				     -1);

		/* Check for user unicity */
		row = sqldb_next_tuple(result);
		if (row) {	/* User is not unique... */
			log_error(NULL,
				  "[xdbsql_authhash_get] user %s not unique", user);
			free(data);
			data = NULL;
		}
	} /* end if */
	else
		log_error(NULL,
			  "[xdbsql_authhash_get] no results, user %s not found", user);
#endif

	sqldb_free_result(result);

	return data;

}				/* end xdbsql_authhash_get */

static char *extract_password(xmlnode data)
{
	char *rc;		/* return from this function */

	if (j_strcmp(xmlnode_get_name(data), "password") != 0) {	/* this node has the wrong name! */
		log_error(NULL, "[xdbsql_auth_set] no <password> found");
		return NULL;

	}
	/* end if */
	rc = xmlnode_get_data(xmlnode_get_firstchild(data));
	if (!rc)
		rc = "";
	return rc;

}				/* end extract_password */

static char *extract_passwordhash(xmlnode data)
{
	char *rc;		/* return from this function */

	if (j_strcmp(xmlnode_get_name(data), "crypt") != 0) {	/* this node has the wrong name! */
		log_error(NULL, "[xdbsql_authhash_set] no <crypt> found");
		return NULL;

	}
	/* end if */
	rc = xmlnode_get_data(xmlnode_get_firstchild(data));
	if (!rc)
		rc = "";
	return rc;

}				/* end extract_password */



int xdbsql_auth_set(XdbSqlDatas * self, const char *user, xmlnode data)
{
	const char *password;	/* the password set by the user */
	const char *qname;	/* query name to use */
	xmlnode query;		/* the actual query to be executed */
	query_def qd;		/* query definition */
	XdbSqlResult *result;	/* return from query */

	password = NULL;


	if (!user) {
		log_error(NULL, "[xdbsql_auth_set] user not specified");
		return 0;

	}

	if (data) {
		password = extract_password(data);
		if (!password)
			return 0;

		/* Figure out which query we want to use. */
		if (xdbsql_user_exists(self, user))
			qname = "auth-set";
		else {
			qname = "auth-set-new";
		}
	} else			/* we're removing the authentication info */
		qname = "auth-disable";

	/* Get the query we need to execute. */
	query = xdbsql_query_get(self, qname);
	if (!query) {
		log_error(NULL, "--!!-- WTF? %s query not found?", qname);
		return 0;
	}

	/* Build the query. */
	qd = xdbsql_querydef_init(self, query);
	xdbsql_querydef_setvar(qd, "user", user);
	xdbsql_querydef_setvar(qd, "password", password);

	/* Execute the query! */
	result = sqldb_query(self, xdbsql_querydef_finalize(qd));
	xdbsql_querydef_free(qd);
	if (!result) {
		log_error(NULL, "[xdbsql_auth_set] query failed : %s",
			  sqldb_error(self));
		return 0;

	}

	sqldb_free_result(result);
	return 1;

}

int xdbsql_authhash_set(XdbSqlDatas * self, const char *user, xmlnode data)
{
	const char *password;	/* the password set by the user */
	const char *qname;	/* query name to use */
	xmlnode query;		/* the actual query to be executed */
	query_def qd;		/* query definition */
	XdbSqlResult *result;	/* return from query */

	password = NULL;


	if (!user) {
		log_error(NULL,
			  "[xdbsql_authhash_set] user not specified");
		return 0;

	}

	if (data) {
		password = extract_passwordhash(data);
		if (!password)
			return 0;

		/* Figure out which query we want to use. */
		if (xdbsql_userhash_exists(self, user))
			qname = "authhash-set";
		else {
			qname = "authhash-set-new";
		}
	} else			/* we're removing the authentication info */
		qname = "authhash-disable";

	/* Get the query we need to execute. */
	query = xdbsql_query_get(self, qname);
	if (!query) {
		log_error(NULL, "--!!-- WTF? %s query not found?", qname);
		return 0;
	}

	/* Build the query. */
	qd = xdbsql_querydef_init(self, query);
	xdbsql_querydef_setvar(qd, "user", user);
	xdbsql_querydef_setvar(qd, "password", password);

	/* Execute the query! */
	result = sqldb_query(self, xdbsql_querydef_finalize(qd));
	xdbsql_querydef_free(qd);
	if (!result) {
		log_error(NULL, "[xdbsql_authhash_set] query failed : %s",
			  sqldb_error(self));
		return 0;

	}

	sqldb_free_result(result);
	return 1;

}

int xdbsql_user_exists(XdbSqlDatas * self, const char *user)
{
	xmlnode query;		/* query to be executed */
	query_def qd;		/* query definition */
	XdbSqlResult *result;	/* pointer to database result */
#ifdef NUM_TUPLES
	int rowcount;		/* number of rows in result */
#endif
	int row;

	if (!user) {
		log_error(NULL, "[xdbsql_user_exists] user not specified");
		return 0;

	}

	/* end if */
	/* Find the XML query definition. */
	query = xdbsql_query_get(self, "checkuser");
	if (!query) {		/* can't check for user existence */
		log_error(NULL, "--!!-- WTF? checkuser query not found?");
		return 0;

	}

	/* end if */
	/* Build the query string. */
	qd = xdbsql_querydef_init(self, query);
	xdbsql_querydef_setvar(qd, "user", user);

	/* Execute the query! */
	result = sqldb_query(self, xdbsql_querydef_finalize(qd));
	xdbsql_querydef_free(qd);
	if (!result) {		/* the query failed - bail out! */
		log_error(NULL, "[xdbsql_user_exists] query failed : %s",
			  sqldb_error(self));
		return 0;

	}

	/* end if */
	/* get the results from the query */
	if (!sqldb_store_result(result)) {	/* unable to retrieve the result */
		log_error(NULL,
			  "[xdbsql_user_exists] result fetch failed : %s",
			  sqldb_error(self));
		return 0;

	}
	/* end if */
#ifdef NUM_TUPLES
	/* get the number of rows in the result - should be >0 if OK, 0 on error */
	rowcount = sqldb_num_tuples(result);
	sqldb_free_result(result);
	return ((rowcount == 0) ? 0 : 1);
#else
	row = sqldb_next_tuple(result);
	sqldb_free_result(result);
	return row;
#endif
}				/* end xdbsql_user_exists */

int xdbsql_userhash_exists(XdbSqlDatas * self, const char *user)
{
	xmlnode query;		/* query to be executed */
	query_def qd;		/* query definition */
	XdbSqlResult *result;	/* pointer to database result */
#ifdef NUM_TUPLES
	int rowcount;		/* number of rows in result */
#endif
	int row;

	if (!user) {
		log_error(NULL,
			  "[xdbsql_userhash_exists] user not specified");
		return 0;

	}

	/* end if */
	/* Find the XML query definition. */
	query = xdbsql_query_get(self, "checkuserhash");
	if (!query) {		/* can't check for user existence */
		log_error(NULL, "--!!-- WTF? checkuser query not found?");
		return 0;

	}

	/* end if */
	/* Build the query string. */
	qd = xdbsql_querydef_init(self, query);
	xdbsql_querydef_setvar(qd, "user", user);

	/* Execute the query! */
	result = sqldb_query(self, xdbsql_querydef_finalize(qd));
	xdbsql_querydef_free(qd);
	if (!result) {		/* the query failed - bail out! */
		log_error(NULL,
			  "[xdbsql_userhash_exists] query failed : %s",
			  sqldb_error(self));
		return 0;

	}

	/* end if */
	/* get the results from the query */
	if (!sqldb_store_result(result)) {	/* unable to retrieve the result */
		log_error(NULL,
			  "[xdbsql_userhash_exists] result fetch failed : %s",
			  sqldb_error(self));
		return 0;

	}
	/* end if */
#ifdef NUM_TUPLES
	/* get the number of rows in the result - should be >0 if OK, 0 on error */
	rowcount = sqldb_num_tuples(result);
	sqldb_free_result(result);
	return ((rowcount == 0) ? 0 : 1);
#else
	row = sqldb_next_tuple(result);
	sqldb_free_result(result);
	return row;
#endif
}				/* end xdbsql_userhash_exists */
