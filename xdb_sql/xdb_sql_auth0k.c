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


static int xdbsql_user_exists_0k(XdbSqlDatas *self, const char *user);

static int
auth0k_disable(XdbSqlDatas *self, const char *user){
    xmlnode query;    /* query nodes */
    query_def qd;	       /* the query definition */
    XdbSqlResult *result;      /* return code from query */

    /* Get the query definitions */
    query = xdbsql_query_get(self, "auth0k-disable");
    if (!query){ /* no query - eep! */
	log_error(NULL,"--!!-- WTF? auth-0k-disable query not found?");
	return 0;

    } /* end if */


    /* Build the query string. */
    qd = xdbsql_querydef_init(self, query);
    xdbsql_querydef_setvar(qd,"user",user);

    /* Execute the query! */
    result = sqldb_query(self, xdbsql_querydef_finalize(qd));
    xdbsql_querydef_free(qd);
    if (!result){ /* the query failed - bail out! */
	log_error(NULL,"[auth0k-disable] query<1> failed : %s",
		  sqldb_error(self));
	return 0;

    } /* end if */

    sqldb_free_result(result);

    return 1;  /* Qa'pla! */

} /* end auth0k-disable */


xmlnode xdbsql_auth0k_get(XdbSqlDatas *self, const char *user){
    xmlnode query;	    /* the actual query to be executed */
    xmlnode data = NULL;    /* return data from this function */
    xmlnode x_hash     = NULL; /* */
    xmlnode x_token    = NULL; /* Nodes for the return tree */
    xmlnode x_sequence = NULL; /* */
    query_def qd;	    /* query definition */
    XdbSqlResult *result;   /* pointer to database result */
#ifdef NUM_TUPLES
    int rowcount;	    /* number of rows in result */
#endif
    short row;		    /* true if there is a row available */
    col_map map;	    /* column mapping */
    int ndx_hash;	    /* index of the hash element */
    int ndx_token;	    /* index of the token element */
    int ndx_sequence;	    /* index of the sequence element */
    const char *querystr;   /* SQL query */

    if (!user){ /* the user was not specified - we have to bug off */
	log_error(NULL,"[xdbsql_auth0k_get] user not specified");
	return NULL;

    } /* end if */

    /* Get the query we need to execute. */
    query = xdbsql_query_get(self, "auth0k-get");

    if (!query){ /* that query should have been specified... */
	log_error(NULL,"--!!-- WTF? auth0k-get query not found?");
	return NULL;

    } /* end if */

    /* Build the query. */
    qd = xdbsql_querydef_init(self, query);
    xdbsql_querydef_setvar(qd,"user",user);

    /* Execute the query! */
    querystr = xdbsql_querydef_finalize(qd);
    result = sqldb_query(self, querystr);
    xdbsql_querydef_free(qd);
    if (!result){ /* the query failed - bye now! */
	log_error(NULL,"[xdbsql_auth0k_get] query failed : %s",
		  sqldb_error(self));
	return NULL;

    } /* end if */

    /* get the results from the query */
    if (!sqldb_store_result(result)){ /* the result fetch failed - bye now! */
	log_error(NULL,"[xdbsql_auth0k_get] result fetch failed : %s",
		  sqldb_error(self));
	return NULL;

    } /* end if */

#ifdef NUM_TUPLES
    /* make sure we got back just one row full of info */
    rowcount = sqldb_num_tuples(result);
    if (rowcount==1){ /* now fetch a row full of data back from the database */
	row = sqldb_next_tuple(result);
	if (row){ /* use a column map to get the index of the "password" field */
	    map = xdbsql_colmap_init(query);
	    ndx_hash = xdbsql_colmap_index(map,"hash");
	    ndx_token = xdbsql_colmap_index(map,"token");
	    ndx_sequence = xdbsql_colmap_index(map,"sequence");
	    xdbsql_colmap_free(map);

	    /* build the result set */
	    data = xmlnode_new_tag("zerok");
	    x_hash = xmlnode_insert_tag(data, "hash");
	    xmlnode_insert_cdata(x_hash,
				 sqldb_get_value(result, ndx_hash), -1);
	    x_token = xmlnode_insert_tag(data, "token");
	    xmlnode_insert_cdata(x_token,
				 sqldb_get_value(result, ndx_token), -1);
	    x_sequence = xmlnode_insert_tag(data, "sequence");
	    xmlnode_insert_cdata(x_sequence,
				 sqldb_get_value(result, ndx_sequence), -1);

	} /* end if */
	else
	    log_error(NULL,"[xdbsql_auth0k_get] row fetch failed");

    } /* end if */
    else{ /* we didn't get back the exact row count we wanted */
	if (rowcount==0)
	    log_error(NULL,"[xdbsql_auth0k_get] no results, user not found");
	else
	    log_error(NULL,"[xdbsql_auth0k_get] user not unique");

    } /* end else */
#else
    row = sqldb_next_tuple(result);
    if (row){ /* use a column map to get the index of the "password" field */
	map = xdbsql_colmap_init(query);
	ndx_hash = xdbsql_colmap_index(map,"hash");
	ndx_token = xdbsql_colmap_index(map,"token");
	ndx_sequence = xdbsql_colmap_index(map,"sequence");
	xdbsql_colmap_free(map);

	/* build the result set */
	data = xmlnode_new_tag("zerok");
	x_hash = xmlnode_insert_tag(data, "hash");
	xmlnode_insert_cdata(x_hash,
			     sqldb_get_value(result, ndx_hash), -1);
	x_token = xmlnode_insert_tag(data, "token");
	xmlnode_insert_cdata(x_token,
				 sqldb_get_value(result, ndx_token), -1);
	x_sequence = xmlnode_insert_tag(data, "sequence");
	xmlnode_insert_cdata(x_sequence,
			     sqldb_get_value(result, ndx_sequence), -1);

	/* Check for user unicity */
	row = sqldb_next_tuple(result);
	if (row){ /* User is not unique... */
	    log_error(NULL,"[xdbsql_auth0k_get] user not unique");
	    free(data);
	    data=NULL;
	}
    }
    else
	log_error(NULL,"[xdbsql_auth0k_get] no results, user not found");
#endif

    sqldb_free_result(result);

    return data;

} /* end xdbsql_auth0k_get */

int xdbsql_auth0k_set(XdbSqlDatas *self, const char *user, xmlnode data){
    const char *hash;	    /* the password set by the user */
    const char *token;	    /* the password set by the user */
    const char *sequence;   /* the password set by the user */
    xmlnode query;	    /* the actual query to be executed */
    query_def qd;	    /* query definition */
    XdbSqlResult *result;   /* return from query */
    const char *querystr;

    if (!user){
	log_error(NULL,"[xdbsql_auth0k_set] user not specified");
	return 0;

    }

    if (xdbsql_user_exists_0k(self, user))
      auth0k_disable(self, user);

    if (data){	   /* get the variables from the node */
      hash = xmlnode_get_tag_data(data, "hash");
      token = xmlnode_get_tag_data(data, "token");
      sequence = xmlnode_get_tag_data(data, "sequence");
    }
    else{
	/* we're removing the authentication info */
	auth0k_disable(self, user);
	return 0;
    }

    /* Get the query we need to execute. */
    query = xdbsql_query_get(self, "auth0k-set");
    if (!query){
	log_error(NULL,"--!!-- WTF? auth0k-set query not found?");
	return 0;
    }

    /* Build the query. */
    qd = xdbsql_querydef_init(self, query);
    xdbsql_querydef_setvar(qd, "user", user);
    xdbsql_querydef_setvar(qd, "hash", hash);
    xdbsql_querydef_setvar(qd, "sequence", sequence);
    xdbsql_querydef_setvar(qd, "token", token);

    /* Execute the query! */
    querystr = xdbsql_querydef_finalize(qd);
    result = sqldb_query(self, querystr);
    xdbsql_querydef_free(qd);
    if (!result){
	log_error(NULL,"[xdbsql_auth0k_set] query failed : %s",
		  sqldb_error(self));
	return 0;

    }

    sqldb_free_result(result);
    return 1;

}

int xdbsql_user_exists_0k(XdbSqlDatas *self, const char *user){
    xmlnode query;	       /* query to be executed */
    query_def qd;	       /* query definition */
    XdbSqlResult *result;      /* pointer to database result */
    int rowcount;	       /* number of rows in result */

    if (!user){
	log_error(NULL,"[xdbsql_user_exists_0k] user not specified");
	return 0;

    } /* end if */

    /* Find the XML query definition. */
    query = xdbsql_query_get(self, "checkuser0k");
    if (!query){ /* can't check for user existence */
	log_error(NULL,"--!!-- WTF? checkuser0k query not found?");
	return 0;

    } /* end if */

    /* Build the query string. */
    qd = xdbsql_querydef_init(self, query);
    xdbsql_querydef_setvar(qd,"user",user);

    /* Execute the query! */
    result = sqldb_query(self,xdbsql_querydef_finalize(qd));
    xdbsql_querydef_free(qd);
    if (!result){ /* the query failed - bail out! */
	log_error(NULL,"[xdbsql_user_exists_0k] query failed : %s",
		  sqldb_error(self));
	return 0;

    } /* end if */

    /* get the results from the query */
    if (!sqldb_store_result(result)){ /* unable to retrieve the result */
	log_error(NULL,"[xdbsql_user_exists_0k] result fetch failed : %s",
		  sqldb_error(self));
	return 0;

    } /* end if */

    /* get the number of rows in the result - should be >0 if OK, 0 on error */
    rowcount = sqldb_num_tuples(result);
    sqldb_free_result(result);
    return ((rowcount==0) ? 0 : 1);

} /* end xdbsql_user_exists_0k */
