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

static int	 process_tuple(XdbSqlResult *result, query_def qd,
			       xmlnode item, xmlnode rc);
static query_def find_purge_query (XdbSqlDatas *self, query_def qd);
static int	 simple_purge (XdbSqlDatas *self, query_def qd,
			       const char *user);
static int	 substitute_params (req_query_def rqd, xmlnode x);
static void	 do_set_query (XdbSqlDatas *self, query_def qd,
			       const char *user, xmlnode x);

/*
 * to do : const-ify libxode/xmlnode
 */

/* Execute a generic XDB get request.
 * self - xdb instance datas
 * qd - the query definition
 * user - owner of the xdb request
 * Returns datas gathered from the database.
 */
xmlnode xdbsql_simple_get(XdbSqlDatas *self, query_def qd, const char *user){
    req_query_def    rqd;	      /* the query definition */
    xmlnode	     rc;	      /* node copy to return */
    xmlnode	     item_tmpl;       /* node to copy for each tuple */
    XdbSqlResult    *sqlres;	      /* pointer to database result */

    if (!user){
	log_error(NULL,"[xdbsql_simple_get] user not specified");
	return NULL;
    }

    rqd = xdbsql_reqquerydef_init(qd, user);

    /* Execute the query! */
    sqlres = sqldb_query(self, xdbsql_reqquerydef_finalize(rqd));
    xdbsql_reqquerydef_free(rqd);
    if (!sqlres){ /* the query failed - bail out! */
	log_error(NULL,"[xdbsql_simple_get] query failed : %s", sqldb_error(self));
	return NULL;
    }

    /* Get the results from the query */
    if (!sqldb_use_result(sqlres)){ /* unable to retrieve the result */
	log_error(NULL,"[xdbsql_simple_get] result fetch failed : %s", sqldb_error(self));
	return NULL;
    }

    /* create result node */
    rc = xmlnode_dup(xdbsql_querydef_get_resnode(qd));


    /* get node to copy on each tuple */
    item_tmpl = xdbsql_querydef_get_tuplenode(qd);

    /* for each tuple */
    while (sqldb_next_tuple(sqlres)){
	xmlnode item = NULL;

	/* create a new current item */
	if (item_tmpl){
	      item = xmlnode_insert_tag_node(rc, item_tmpl);
	}
	/* for each value/field in tuple (as given by bindcol items) */

	process_tuple(sqlres, qd, item, rc);

    } /* while tuple */


    sqldb_free_result(sqlres);

    return rc;
}

static int process_tuple(XdbSqlResult *sqlres, query_def qd,
			 xmlnode item, xmlnode rc){
    col_map_entry    bcol;
    int		     has_value;

    has_value = 0;

    for (bcol = xdbsql_querydef_get_firstcol(qd);
	 bcol != NULL; bcol = xdbsql_querydef_get_nextcol(bcol)){
	const char *sptr;
	const char *into;
	xmlnode dest = NULL;	/* where a value is going to be inserted */
	char scanbuf[200];
	int  len;

	/* process value */
	sptr = sqldb_get_value(sqlres, xdbsql_querydef_col_get_offset(bcol));

	if (!sptr || !*sptr){
	    continue;
	}

	has_value = 1;

	len = strlen(sptr);
	if ((len > 0) && (len < 200) && (sptr[len-1] == ' ')){
	    *scanbuf = 0;
	    sscanf(sptr, "%s", scanbuf);
	    sptr = scanbuf;
	}

	/* Where should we insert this value ?
	 * If not specified, it will be in item, then in rc.
	 */
	into = xdbsql_querydef_col_get_into(bcol);
	if (!into){
	    if (item)
		dest = item;
	    else
		dest = rc;
	}
	else{
	    /* Search the node starting from the item. */
	    if (!j_strcmp(xmlnode_get_name(item), into))
		dest = item;
	    if (!dest)
		dest = xmlnode_get_tag(item, into);
	    if (!dest){
		/* Not found, search from rc */
		if (!j_strcmp(xmlnode_get_name(rc), into))
		    dest = rc;
	    }
	    if (!dest)
		dest = xmlnode_get_tag(rc, into);
	}

	if (!dest){
	    log_error(NULL,
		      "[xdbsql_simple_get] cannot handle bindcol :"
		      " insertion point '%s' not found in node %s",
		      into, xmlnode2str(rc));
	    return 0;
	}

	/* do the insert */
	if (xdbsql_querydef_col_get_instype(bcol) == INS_TAG){
	    xmlnode tag =
		xmlnode_insert_tag(dest,
				   xdbsql_querydef_col_get_name(bcol));
	    xmlnode_insert_cdata(tag, sptr, -1);
	}
	else{
	    xmlnode_put_attrib(dest, xdbsql_querydef_col_get_name(bcol), sptr);
	}
    } /* for bcol */

    return has_value;
}

static int
simple_purge (XdbSqlDatas *self, query_def qd, const char *user){
    query_def	     pq;
    req_query_def    rqd;	      /* the query definition */
    XdbSqlResult    *sqlres;	      /* pointer to database result */

    pq = find_purge_query(self, qd);
    if (!pq)
	return 1;

    rqd = xdbsql_reqquerydef_init(pq, user);

    /* Execute the query! */
    sqlres = sqldb_query(self, xdbsql_reqquerydef_finalize(rqd));
    xdbsql_reqquerydef_free(rqd);
    if (!sqlres){ /* the query failed - bail out! */
	log_error(NULL,"[simple_purge] query failed : %s", sqldb_error(self));
	return 0;
    }

    sqldb_free_result(sqlres);
    return 1;
}

static query_def find_purge_query (XdbSqlDatas *self, query_def qd){
    query_def	pq;
    query_node	qn;
    const char *pq_name;

    pq = NULL;
    pq_name = xdbsql_querydef_get_purge(qd);
    if (!pq_name)
	return NULL;
    for (qn = self->queries_v2; qn != NULL; qn = qn->next){
	if (qn->query_name && !strcmp(qn->query_name, pq_name)){
	    pq = qn->qd;
	    break;
	}
    }
    if (!pq)
	log_error(NULL,"[find_purge_query] query '%s' not found", pq_name);

    return pq;
}

/*
 * datas - if not NULL, datas to set
 */
int xdbsql_simple_set (XdbSqlDatas *self, query_def qd,
		       const char *user, xmlnode datas){
    if (!user){
	log_error(NULL,"[xdbsql_query_handler_v2] user not specified");
	return 0;
    }
    simple_purge(self, qd, user);
    do_set_query(self, qd, user, datas);
    return 1;
}

static void do_set_query (XdbSqlDatas *self, query_def qd, const char *user, xmlnode x){
    req_query_def    rqd;	      /* the query definition */
    XdbSqlResult     *sqlres;	       /* pointer to database result */
    xmlnode	     temp;

    rqd = xdbsql_reqquerydef_init(qd, user);

    /* log_debug("[do_set_query] got xmlnode : %s", xmlnode2str(x)); */

    /* single node : substitute it in one block */
    if (!xmlnode_get_firstchild(x)){
	if (!substitute_params(rqd, x)){
	    xdbsql_reqquerydef_free(rqd);
	    return;
	}
    }
    /* not a single node : substitute each tag in the request */
    else{
	for (temp = xmlnode_get_firstchild(x); temp != NULL;
	     temp = xmlnode_get_nextsibling(temp)){
	    if (xmlnode_get_type(temp) != NTYPE_TAG)
		continue;

	    if (!substitute_params(rqd, temp)){
		xdbsql_reqquerydef_free(rqd);
		return;
	    }
	}
    }

    /* Execute the query! */
    sqlres = sqldb_query(self, xdbsql_reqquerydef_finalize(rqd));
    xdbsql_reqquerydef_free(rqd);
    if (!sqlres){
	/* the query failed - bail out! */
	log_error(NULL,"[xdbsql_simple_get] query failed : %s", sqldb_error(self));
	return;
    }

    sqldb_free_result(sqlres);
}

static int substitute_params (req_query_def rqd, xmlnode x){
    query_def_bind_var var;

    /* log_debug("[susbtitute_params] dealing with xmlnode : %s", xmlnode2str(x)); */

    /* for each var */
    for (var = xdbsql_reqquerydef_get_firstvar(rqd);
	 var != NULL; var = xdbsql_reqquerydef_get_nextvar(var)){
	const char *sptr;
	const char *name;

	name = xdbsql_var_get_name(var);

	if (xdbsql_var_is_attrib(var)){
	    sptr = xmlnode_get_attrib(x, name);
	}
	else{
	    if (!j_strcmp(xmlnode_get_name(x), name))
		sptr = xmlnode_get_data(x);
	    else
		sptr = xmlnode_get_tag_data(x, name);
	}
	if (sptr && *sptr){
	    /* log_debug("[substitute_params] set %s to %s", sptr, name); */

	    xdbsql_querydef_setvar((query_def)rqd, name, sptr);
	    return 1;
	}
    }

    return 0;
}

