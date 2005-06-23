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

xmlnode xdbsql_register_get(XdbSqlDatas *self, const char *user){
    xmlnode query;	    /* the actual query to be executed */
    xmlnode data = NULL;    /* return data from this function */
    query_def qd;	    /* query definition */
    XdbSqlResult *result;   /* pointer to database result */
    int first = 1;	    /* is this the first runthrough? */
    col_map map;	    /* column mapping */
    int ndx_resource;	    /* index of the resource element */

    if (!user){ /* the user was not specified - we have to bug off */
	log_error(NULL,"[xdbsql_register_get] user not specified");
	return NULL;

    } /* end if */

    /* Get the query we need to execute. */
    query = xdbsql_query_get(self, "resource-get");
    if (!query){ /* that query should have been specified... */
	log_error(NULL,"--!!-- WTF? resource-get query not found?");
	return NULL;

    } /* end if */

    /* Build the query. */
    qd = xdbsql_querydef_init(self, query);
    xdbsql_querydef_setvar(qd,"user",user);

    /* Execute the query! */
    result = sqldb_query(self,xdbsql_querydef_finalize(qd));
    xdbsql_querydef_free(qd);
    if (!result){ /* the query failed - bye now! */
	log_error(NULL,"[xdbsql_register_get] query failed : %s", sqldb_error(self));
	return NULL;

    } /* end if */

    /* get the results from the query */
    if (!sqldb_use_result(result)){ /* the result fetch failed - bye now! */
	log_error(NULL,"[xdbsql_register_get] result fetch failed : %s", sqldb_error(self));
	return NULL;

    } /* end if */

    /* create the return xmlnode */
    data = xmlnode_new_tag("query");
    xmlnode_put_attrib(data,"xmlns",NS_REGISTER);

    while (sqldb_next_tuple(result)){ /* load all the resources as tags */
	if (first){ /* initialize the column index */
	    map = xdbsql_colmap_init(query);
	    ndx_resource = xdbsql_colmap_index(map,"resource");
	    xdbsql_colmap_free(map);
	    first = 0;

	} /* end if */

	/* add the "resource" tag */
	xmlnode_insert_cdata(xmlnode_insert_tag(data,"resource"),
			     sqldb_get_value(result, ndx_resource),-1);

    } /* end while */

    sqldb_free_result(result);

    return data;

} /* end xdbsql_register_get */

static int purge_register(XdbSqlDatas *self,
			  const char *user){
    xmlnode query;	    /* the actual query to be executed */
    query_def qd;	    /* query definition */
    XdbSqlResult *result;   /* return from query */

    /* Get the query we need to execute. */
    query = xdbsql_query_get(self, "resource-remove");
    if (!query){ /* that query should have been specified... */
	log_error(NULL,"--!!-- WTF? resource-remove query not found?");
	return 0;

    } /* end if */

    /* Build the query. */
    qd = xdbsql_querydef_init(self, query);
    xdbsql_querydef_setvar(qd,"user",user);

    /* Execute the query! */
    result = sqldb_query(self,xdbsql_querydef_finalize(qd));
    xdbsql_querydef_free(qd);
    if (!result){ /* the query failed - bye now! */
	log_error(NULL,"[purge_register] query failed : %s", sqldb_error(self));
	return 0;

    } /* end if */
    sqldb_free_result(result);

    return 1;  /* Qa'pla! */

} /* end purge_register */

static int test_root_register_node(xmlnode root){
    if (j_strcmp(xmlnode_get_name(root),"query")!=0){ /* it's not an IQ query node */
	log_error(NULL,"[xdbsql_register_set] not a <query> node");
	return 0;

    } /* end if */

    if (j_strcmp(xmlnode_get_attrib(root,"xmlns"),NS_REGISTER)!=0){ /* it's not got the right namespace */
	log_error(NULL,"[xdbsql_register_set] not a jabber:iq:roster node");
	return 0;

    } /* end if */
    return 1;  /* ship it */

} /* end test_root_register_node */

int xdbsql_register_set(XdbSqlDatas *self, const char *user, xmlnode data){
    xmlnode query;	    /* the actual query to be executed */
    xmlnode tmp;	    /* for cycling through data children */
    query_def qd;	    /* query definition */
    XdbSqlResult *result;   /* return from query */

    if (!user){ /* the user was not specified - we have to bug off */
	log_error(NULL,"[xdbsql_register_set] user not specified");
	return 0;

    } /* end if */

    if (!data)	/* we're erasing the data */
	return purge_register(self, user);

    if (!test_root_register_node(data))
	return 0;  /* error already reported */

    /* Get the query we need to execute. */
    query = xdbsql_query_get(self, "resource-set");
    if (!query){ /* that query should have been specified... */
	log_error(NULL,"--!!-- WTF? resource-set query not found?");
	return 0;

    } /* end if */

    /* Remove all existing entries for a user */
    (void)purge_register(self, user);

    for (tmp=xmlnode_get_firstchild(data); tmp; tmp=xmlnode_get_nextsibling(tmp)){ /* add all resource nodes under here to the database */
	if (j_strcmp(xmlnode_get_name(tmp),"resource")==0){ /* Build the query. */
	    qd = xdbsql_querydef_init(self, query);
	    xdbsql_querydef_setvar(qd,"user",user);
	    xdbsql_querydef_setvar(qd,"resource",
				     xmlnode_get_data(xmlnode_get_firstchild(tmp)));

	    /* Execute the query! */
	    result = sqldb_query(self,xdbsql_querydef_finalize(qd));
	    xdbsql_querydef_free(qd);
	    if (!result)
		log_error(NULL,"[xdbsql_register_set] query failed for add of %s/%s : %s",
			  user,xmlnode_get_data(xmlnode_get_firstchild(tmp)), sqldb_error(self));
	    else
		sqldb_free_result(result);
	} /* end if */

    } /* end for */

    return 1;  /* Qa'pla! */

} /* end xdbsql_register_set */

