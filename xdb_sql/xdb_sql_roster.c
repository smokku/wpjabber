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

static int roster_purge(XdbSqlDatas *self,
    const char *user){
    xmlnode query1, query2;    /* query nodes */
    query_def qd;	       /* the query definition */
    XdbSqlResult *result;      /* return code from query */

    /* Get the query definitions */
    query1 = xdbsql_query_get(self, "roster-purge-1");
    if (!query1){ /* no query - eep! */
	log_error(NULL,"--!!-- WTF? roster-purge-1 query not found?");
	return 0;

    } /* end if */

    query2 = xdbsql_query_get(self, "roster-purge-2");
    if (!query2){ /* no query - eep! */
	log_error(NULL,"--!!-- WTF? roster-purge-2 query not found?");
	return 0;

    } /* end if */

    /***** Pass 1: Purge roster elements *****/

    /* Build the query string. */
    qd = xdbsql_querydef_init(self, query1);
    xdbsql_querydef_setvar(qd,"user",user);

    /* Execute the query! */
    result = sqldb_query(self, xdbsql_querydef_finalize(qd));
    xdbsql_querydef_free(qd);
    if (!result){ /* the query failed - bail out! */
	log_error(NULL,"[roster_purge] query<1> failed : %s", sqldb_error(self));
	return 0;

    } /* end if */

    sqldb_free_result(result);

    /***** Pass 2: Purge roster groups *****/

    /* Build the query string. */
    qd = xdbsql_querydef_init(self, query2);
    xdbsql_querydef_setvar(qd,"user",user);

    /* Execute the query! */
    result = sqldb_query(self, xdbsql_querydef_finalize(qd));
    xdbsql_querydef_free(qd);
    if (!result){ /* the query failed - bail out! */
	log_error(NULL,"[roster_purge] query<2> failed : %s", sqldb_error(self));
	return 0;

    } /* end if */

    sqldb_free_result(result);

    return 1;  /* Qa'pla! */

} /* end roster_purge */

xmlnode xdbsql_roster_get(XdbSqlDatas *self, const char *user){
    xmlnode rc = NULL;	       /* return from this function */
    xmlnode query1, query2;    /* query nodes */
    xmlnode x1, x2;	       /* temporary node pointers */
    query_def qd;	       /* the query definition */
    XdbSqlResult *result;      /* pointer to database result */
    int first = 1;	       /* first time through loop? */
    spool s;		       /* string pooling */
    int ndx_jid, ndx_nickname, ndx_subscription, ndx_ask, ndx_type, ndx_ext, ndx_group;
    int ndx_subscribe;

    const char *tmp_attr;      /* temporary attribute value */
    const char *tmp_str;

    if (!user){ /* the user was not specified - we have to bug off */
	log_error(NULL,"[xdbsql_roster_get] user not specified");
	return NULL;

    } /* end if */

    /* Get the query definitions */
    query1 = xdbsql_query_get(self, "roster-load-1");
    if (!query1){ /* no query - eep! */
	log_error(NULL,"--!!-- WTF? roster-load-1 query not found?");
	return NULL;

    } /* end if */

    query2 = xdbsql_query_get(self, "roster-load-2");
    if (!query2){ /* no query - eep! */
	log_error(NULL,"--!!-- WTF? roster-load-2 query not found?");
	return NULL;

    } /* end if */

    /***** Pass 1: Load roster elements *****/

    /* Build the query string. */
    qd = xdbsql_querydef_init(self, query1);
    xdbsql_querydef_setvar(qd,"user",user);

    /* Execute the query! */
    result = sqldb_query(self, xdbsql_querydef_finalize(qd));
    xdbsql_querydef_free(qd);
    if (!result){ /* the query failed - bail out! */
	log_error(NULL,"[xdbsql_roster_get] query failed : %s", sqldb_error(self));
	return NULL;

    } /* end if */

    /* get the results from the query */
    if (!sqldb_use_result(result)){ /* unable to retrieve the result */
	log_error(NULL,"[xdbsql_roster_get] result fetch failed : %s", sqldb_error(self));
	return NULL;

    } /* end if */

    /* Initialize the return value. */
    rc = xmlnode_new_tag("query");
    xmlnode_put_attrib(rc,"xmlns",NS_ROSTER);

    while (sqldb_next_tuple(result)!=0){ /* look for all roster entries and add them to our output tree */
	if (first){ /* initialize the column mapping indexes */
	    col_map map = xdbsql_colmap_init(query1);
	    ndx_jid = xdbsql_colmap_index(map,"jid");
	    ndx_nickname = xdbsql_colmap_index(map,"nickname");
	    ndx_subscription = xdbsql_colmap_index(map,"subscription");
	    ndx_ask = xdbsql_colmap_index(map,"ask");
	    ndx_subscribe = xdbsql_colmap_index(map,"subscribe");
	    ndx_ext = xdbsql_colmap_index(map,"x");
	    ndx_type = xdbsql_colmap_index(map,"type");
	    xdbsql_colmap_free(map);
	    first = 0;

	} /* end if */

	/* first let's see what type of rosteritem it is */
	tmp_attr = sqldb_get_value(result, ndx_type);
	if (!tmp_attr){
	    log_error(NULL,"[xdbsql_roster_get] error getting 'type' field\n");
	    return NULL;
	}
	if (strcmp(tmp_attr,"item") != 0)
	    tmp_attr = "conference";

	/* get the extensions and start building the item tree */
	tmp_str = sqldb_get_value(result, ndx_ext);
	if (!tmp_str){
	    log_error(NULL,"[xdbsql_roster_get] error getting 'extension' field\n");
	    return NULL;
	}
	if (tmp_str && *tmp_str){
	    xmlnode node;
	    char *itemname;
	    /* this is tricky because there may be multiple <x> tags; parse them with
	     * the <itemtype></itemtype> (tmp_attr) surounding them
	     */
	    s = spool_new(xmlnode_pool(rc));
	    spooler(s,"<",tmp_attr,">",tmp_str,"</",tmp_attr,">",s);
	    tmp_str = spool_print(s);
	    node = xmlnode_str((char *)tmp_str,strlen(tmp_str));
	    x1 = xmlnode_insert_tag_node(rc, node);
	    xmlnode_free(node);
	}
	else{ /* just create a new item or conference under the result. */
	       /* depending on the type of the record */
	    x1 = xmlnode_insert_tag(rc,tmp_attr);
	}
	/* insert "type" attribute if it is a conference */
	if (strcmp(tmp_attr,"conference") == 0){
	    xmlnode_put_attrib(x1,"type",sqldb_get_value(result, ndx_type));
	}

	/* move to the item attributes */
	xmlnode_put_attrib(x1,"jid",sqldb_get_value(result, ndx_jid));

	tmp_attr = sqldb_get_value(result, ndx_nickname);
	if (tmp_attr && *tmp_attr)
	    xmlnode_put_attrib(x1,"name",tmp_attr);
	tmp_attr = NULL;

	tmp_str = sqldb_get_value(result, ndx_subscription);
	if (!tmp_str){
	    log_error(NULL,"[xdbsql_roster_get] error getting 'subscription' field\n");
	    return NULL;
	}
	switch (*tmp_str){ /* translate the subscription character flag into an attribute value */
	case 'N':
	    tmp_attr = "none";
	    break;

	case 'T':
	    tmp_attr = "to";
	    break;

	case 'F':
	    tmp_attr = "from";
	    break;

	case 'B':
	    tmp_attr = "both";
	    break;

	} /* end switch */

	if (tmp_attr)
	    xmlnode_put_attrib(x1,"subscription",tmp_attr);
	tmp_attr = NULL;
	tmp_str = sqldb_get_value(result, ndx_ask);
	if (!tmp_str){
	    log_error(NULL,"[xdbsql_roster_get] error getting 'ask' field\n");
	    return NULL;
	}

	switch (*tmp_str){ /* translate the "ask" character flag into an attribute value */
	case 'S':
	    tmp_attr = "subscribe";
	    break;

	case 'U':
	    tmp_attr = "unsubscribe";
	    break;

	} /* end switch */

	if (tmp_attr)
	    xmlnode_put_attrib(x1,"ask",tmp_attr);
	tmp_attr = NULL;

	tmp_str = sqldb_get_value(result, ndx_subscribe);
	if (!tmp_str){
	    log_error(NULL,"[xdbsql_roster_get] error getting 'subscribe' field\n");
	    return NULL;
	}
	// if subscribe string is a dash, add subscribe attribute.
	if (tmp_str && tmp_str[0] == '-')
	    xmlnode_put_attrib(x1,"subscribe","");
	else if (tmp_str && *tmp_str)
	    xmlnode_put_attrib(x1,"subscribe",tmp_str);

    } /* end while */

    sqldb_free_result(result);	/* all done with this query */

    /***** Pass 2: Load roster groups *****/

    first = 1;
    for (x1=xmlnode_get_firstchild(rc); x1; x1=xmlnode_get_nextsibling(x1)){ /* Build the query string. */
	const char *querystring;
	qd = xdbsql_querydef_init(self, query2);
	xdbsql_querydef_setvar(qd,"user",user);
	xdbsql_querydef_setvar(qd,"jid",xmlnode_get_attrib(x1,"jid"));

	/* Execute the query! */
	querystring = xdbsql_querydef_finalize(qd);
	result = sqldb_query(self,querystring);
	xdbsql_querydef_free(qd);
	if (!result){ /* the query failed - bail out! */
	    log_warn(ZONE,"[xdbsql_roster_get] groups query for %s/%s failed : %s",
		     user,xmlnode_get_attrib(x1,"jid"), sqldb_error(self));
	    continue;

	} /* end if */

	/* get the results from the query */
	if (!sqldb_use_result(result)){ /* unable to retrieve the result */
	    log_warn(ZONE,"[xdbsql_roster_get] groups result get for %s/%s "
		     "failed, unable to retrieve results : %s",
		     user,xmlnode_get_attrib(x1,"jid"), sqldb_error(self));
	    continue;

	} /* end if */

	while (sqldb_next_tuple(result)!=0){ /* look for all group entries and add them to our output tree */
	    if (first){ /* initialize the column mapping indexes */
		col_map map = xdbsql_colmap_init(query2);
		ndx_group = xdbsql_colmap_index(map,"grp");
		xdbsql_colmap_free(map);
		first = 0;

	    } /* end if */

	    /* add a <group> tag under the <item> one for this group */
	    x2 = xmlnode_insert_tag(x1,"group");
	    xmlnode_insert_cdata(x2,sqldb_get_value(result, ndx_group),-1);

	} /* end while */

	sqldb_free_result(result);  /* done with this result */

    } /* end for */

    return rc;	/* all done with this operation! */

} /* end xdbsql_roster_get */

static int test_root_roster_node(
    xmlnode root){
    if (j_strcmp(xmlnode_get_name(root),"query")!=0){ /* it's not an IQ query node */
	log_error(NULL,"[xdbsql_roster_set] not a <query> node");
	return 0;

    } /* end if */

    if (j_strcmp(xmlnode_get_attrib(root,"xmlns"),NS_ROSTER)!=0){ /* it's not got the right namespace */
	log_error(NULL,"[xdbsql_roster_set] not a jabber:iq:roster node");
	return 0;

    } /* end if */

    return 1;  /* ship it */

} /* end test_root_roster_node */

int xdbsql_roster_set(XdbSqlDatas *self, const char *user, xmlnode roster){
    xmlnode query1, query2;    /* query nodes */
    xmlnode x1, x2;	       /* temporary node pointers */
    query_def qd;	       /* the query definition */
    XdbSqlResult *result;      /* return from query */
    const char *tmp_attr;      /* temporary for getting attributes */
    char tmp_var[2];	       /* temporary for single-char variables */
    const char *querystr;      /* SQL query */
    spool data_ext;	       /* extension data string pool */

    if (!user){ /* roster user not specified */
	log_error(NULL,"[xdbsql_roster_set] user not specified");
	return 0;

    } /* end if */

    if (!roster)  /* NULL means we're removing the data */
	return roster_purge(self, user);

    if (!test_root_roster_node(roster))
	return 0;  /* error already reported */

    /* Get the query definitions */
    query1 = xdbsql_query_get(self, "roster-add-1");
    if (!query1){ /* no query - eep! */
	log_error(NULL,"--!!-- WTF? roster-add-1 query not found?");
	return 0;

    } /* end if */

    query2 = xdbsql_query_get(self, "roster-add-2");
    if (!query2){ /* no query - eep! */
	log_error(NULL,"--!!-- WTF? roster-add-2 query not found?");
	return 0;

    } /* end if */

    /* Get rid of any existing roster entries for the user. */
    (void)roster_purge(self, user);

    tmp_var[1] = '\0';
    for (x1=xmlnode_get_firstchild(roster); x1; x1=xmlnode_get_nextsibling(x1)){ /* create the query definition and begin binding variables */
	qd = xdbsql_querydef_init(self, query1);
	xdbsql_querydef_setvar(qd,"user",user);
	xdbsql_querydef_setvar(qd,"jid",xmlnode_get_attrib(x1,"jid"));

	tmp_attr = xmlnode_get_attrib(x1,"name");
	if (tmp_attr && *tmp_attr)
	    xdbsql_querydef_setvar(qd,"nickname",tmp_attr);
	else
	    xdbsql_querydef_setvar(qd,"nickname","");

	/* bind the "subscription" variable */
	tmp_var[0] = '\0';
	tmp_attr = xmlnode_get_attrib(x1,"subscription");
	if (j_strcmp(tmp_attr,"none")==0)
	    tmp_var[0] = 'N';
	else if (j_strcmp(tmp_attr,"to")==0)
	    tmp_var[0] = 'T';
	else if (j_strcmp(tmp_attr,"from")==0)
	    tmp_var[0] = 'F';
	else if (j_strcmp(tmp_attr,"both")==0)
	    tmp_var[0] = 'B';
	if (tmp_var[0])
	    xdbsql_querydef_setvar(qd,"subscription",tmp_var);

	/* bind the "ask" variable */
	tmp_var[0] = '-';
	tmp_attr = xmlnode_get_attrib(x1,"ask");
	if (j_strcmp(tmp_attr,"subscribe")==0)
	    tmp_var[0] = 'S';
	else if (j_strcmp(tmp_attr,"unsubscribe")==0)
	    tmp_var[0] = 'U';
	xdbsql_querydef_setvar(qd,"ask",tmp_var);

	/* bind the "subscribe" variable */
	tmp_attr = NULL;
	tmp_attr = xmlnode_get_attrib(x1,"subscribe");
	if (tmp_attr != NULL){
	  if (tmp_attr[0] == '\0')
	      // if subscribe attribute is present, and empty, store a dash instead.
	      xdbsql_querydef_setvar(qd,"subscribe", "-");
	  else
	      xdbsql_querydef_setvar(qd,"subscribe",tmp_attr);
	} else{
	    xdbsql_querydef_setvar(qd,"subscribe","");
	}

	/* bind the "type" variable */
	tmp_attr = xmlnode_get_attrib(x1,"type");
	if (tmp_attr != NULL){
	    xdbsql_querydef_setvar(qd,"type",tmp_attr);
	} else{
	    xdbsql_querydef_setvar(qd,"type","item");
	}

	/* get the "eXtension"'s nodes */
	data_ext = spool_new(xmlnode_pool(roster));
	for (x2 = xmlnode_get_firstchild(x1); x2; x2 = xmlnode_get_nextsibling(x2)){ /* handle all the child nodes of the item */
	    if (j_strcmp("x", xmlnode_get_name(x2)))
		continue;
	    spool_add(data_ext,xmlnode2str(x2));
	}
	xdbsql_querydef_setvar(qd,"x",spool_print(data_ext));

	/* Execute the query! */
	querystr = xdbsql_querydef_finalize(qd);
	result = sqldb_query(self,querystr);
	xdbsql_querydef_free(qd);
	if (!result){ /* the query failed - bail out! */
	    log_warn(ZONE,"[xdbsql_roster_set] query failure on header"
		     " for %s/%s : %s",user,xmlnode_get_attrib(x1,"jid"),
		     sqldb_error(self));
	    continue;

	} /* end if */
	sqldb_free_result(result);

	tmp_attr = xmlnode_get_attrib(x1,"jid");
	for (x2=xmlnode_get_firstchild(x1); x2; x2=xmlnode_get_nextsibling(x2)){ /* create query to add group and bind its variables */
	    const char *grp;

	    if (j_strcmp("group", xmlnode_get_name(x2)))
		continue;
	    qd = xdbsql_querydef_init(self, query2);
	    xdbsql_querydef_setvar(qd,"user",user);
	    xdbsql_querydef_setvar(qd,"jid",tmp_attr);
	    grp = xmlnode_get_data(xmlnode_get_firstchild(x2));
	    xdbsql_querydef_setvar(qd,"grp", grp);

	    /* Execute the query! */
	    result = sqldb_query(self,xdbsql_querydef_finalize(qd));
	    xdbsql_querydef_free(qd);
	    if (!result){ /* the query failed - bail out! */
		log_warn(ZONE,"[xdbsql_roster_set] query failure on groups"
			 " for %s/%s, halting group add : %s",user,tmp_attr,
			 sqldb_error(self));
		break;

	    } /* end if */
	    sqldb_free_result(result);
	} /* end for */
    } /* end for */

    return 1;  /* Qa'pla! */

} /* end xdbsql_roster_set */
