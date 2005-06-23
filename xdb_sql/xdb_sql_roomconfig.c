/*
 * author: Felipe Madero
 */

#include "xdb_sql.h"

#define GET_CHILD_DATA(X) xmlnode_get_data(xmlnode_get_firstchild(X));

xmlnode
xdbsql_roomconfig_get(XdbSqlDatas *self, const char *room){
    xmlnode node = NULL;       /* return from this function */
    xmlnode query;	       /* query node */
    xmlnode element;
    query_def qd;	       /* the query definition */
    XdbSqlResult *result;      /* pointer to database result */
    int first = 1;
    int tuples;

    int ndx_owner, ndx_name, ndx_secret, ndx_private, ndx_leave, ndx_join;
    int ndx_rename, ndx_public, ndx_subjectlock, ndx_maxusers, ndx_moderated;
    int ndx_defaultype, ndx_privmsg, ndx_invitation, ndx_invites, ndx_legacy;
    int ndx_visible, ndx_logformat, ndx_logging, ndx_description;
    int ndx_persistant;

    char* secret;

    if (!room){
	/* the room was not specified - we have to bug off */
	log_error(NULL,"[xdbsql_roomconfig_get] room not specified");
	return NULL;
    }

    /* Get the query definitions */
    query = xdbsql_query_get(self, "roomconfig-get");
    if (!query){
	log_error(NULL,"--!!-- WTF? roomconfig-get query not found?");
	return NULL;

    }

    /* Load roomconfig elements */

    /* Build the query string. */
    qd = xdbsql_querydef_init(self, query);
    xdbsql_querydef_setvar(qd,"room", room);

    /* Execute the query! */
    result = sqldb_query(self, xdbsql_querydef_finalize(qd));
    xdbsql_querydef_free(qd);
    if (!result){
	log_error(NULL,"[xdbsql_roomconfig_get] query failed : %s", sqldb_error(self));
	return NULL;
    }

    /* get the results from the query */
    if (!sqldb_use_result(result)){
	log_error(NULL,"[xdbsql_roomconfig_get] result fetch failed : %s", sqldb_error(self));
	return NULL;
    }

    tuples = 0;
    while (sqldb_next_tuple(result)!=0){
	tuples++;
	if (tuples != 1)
	    break;
	/* look for all room entries and add them to our output tree */
	if (first){
	    /* initialize the column mapping indexes */
	    col_map map = xdbsql_colmap_init(query);
	    ndx_owner = xdbsql_colmap_index(map,"owner");
	    ndx_name = xdbsql_colmap_index(map,"name");
	    ndx_secret = xdbsql_colmap_index(map,"secret");
	    ndx_private = xdbsql_colmap_index(map,"private");
	    ndx_leave = xdbsql_colmap_index(map,"leave");
	    ndx_join = xdbsql_colmap_index(map,"join");
	    ndx_rename = xdbsql_colmap_index(map,"rename");
	    ndx_persistant = xdbsql_colmap_index(map,"persistant");
	    ndx_public = xdbsql_colmap_index(map,"public");
	    ndx_subjectlock = xdbsql_colmap_index(map,"subjectlock");
	    ndx_maxusers = xdbsql_colmap_index(map,"maxusers");
	    ndx_moderated = xdbsql_colmap_index(map,"moderated");
	    ndx_defaultype = xdbsql_colmap_index(map,"defaultype");
	    ndx_privmsg = xdbsql_colmap_index(map,"privmsg");
	    ndx_invitation = xdbsql_colmap_index(map,"invitation");
	    ndx_invites = xdbsql_colmap_index(map,"invites");
	    ndx_legacy = xdbsql_colmap_index(map,"legacy");
	    ndx_visible = xdbsql_colmap_index(map,"visible");
	    ndx_logformat = xdbsql_colmap_index(map,"logformat");
	    ndx_logging = xdbsql_colmap_index(map,"logging");
	    ndx_description = xdbsql_colmap_index(map,"description");
	    xdbsql_colmap_free(map);
	    first = 0;
	}

	node = xmlnode_new_tag("room");

	xmlnode_put_attrib(xmlnode_insert_tag(node, "owner"), "jid",
	    sqldb_get_value(result, ndx_owner));
	xmlnode_insert_cdata(xmlnode_insert_tag(node, "name"),
	    sqldb_get_value(result, ndx_name), -1);
	secret = (char*) sqldb_get_value(result, ndx_secret);
	if (!secret || j_strcmp(secret, "") == 0)
	  xmlnode_insert_cdata(xmlnode_insert_tag(node, "secret"), NULL, -1);
	else
	  xmlnode_insert_cdata(xmlnode_insert_tag(node, "secret"), secret, -1);
	xmlnode_insert_cdata(xmlnode_insert_tag(node, "private"),
	    sqldb_get_value(result, ndx_private), -1);

	element = xmlnode_insert_tag(node, "notice");
	xmlnode_insert_cdata(xmlnode_insert_tag(element, "leave"),
	    sqldb_get_value(result, ndx_leave), -1);
	xmlnode_insert_cdata(xmlnode_insert_tag(element, "join"),
	    sqldb_get_value(result, ndx_join), -1);
	xmlnode_insert_cdata(xmlnode_insert_tag(element, "rename"),
	    sqldb_get_value(result, ndx_rename), -1);

	xmlnode_insert_cdata(xmlnode_insert_tag(node, "public"),
	    sqldb_get_value(result, ndx_public), -1);
	xmlnode_insert_cdata(xmlnode_insert_tag(node, "subjectlock"),
	    sqldb_get_value(result, ndx_subjectlock), -1);
	xmlnode_insert_cdata(xmlnode_insert_tag(node, "maxusers"),
	    sqldb_get_value(result, ndx_maxusers), -1);
	xmlnode_insert_cdata(xmlnode_insert_tag(node, "persistant"),
	    sqldb_get_value(result, ndx_persistant), -1);
	xmlnode_insert_cdata(xmlnode_insert_tag(node, "moderated"),
	    sqldb_get_value(result, ndx_moderated), -1);
	xmlnode_insert_cdata(xmlnode_insert_tag(node, "defaulttype"),
	    sqldb_get_value(result, ndx_defaultype), -1);
	xmlnode_insert_cdata(xmlnode_insert_tag(node, "privmsg"),
	    sqldb_get_value(result, ndx_privmsg), -1);
	xmlnode_insert_cdata(xmlnode_insert_tag(node, "invitation"),
	    sqldb_get_value(result, ndx_invitation), -1);
	xmlnode_insert_cdata(xmlnode_insert_tag(node, "invites"),
	    sqldb_get_value(result, ndx_invites), -1);
	xmlnode_insert_cdata(xmlnode_insert_tag(node, "legacy"),
	    sqldb_get_value(result, ndx_legacy), -1);
	xmlnode_insert_cdata(xmlnode_insert_tag(node, "visible"),
	    sqldb_get_value(result, ndx_visible), -1);
	xmlnode_insert_cdata(xmlnode_insert_tag(node, "logformat"),
	    sqldb_get_value(result, ndx_logformat), -1);

	    xmlnode_insert_cdata(xmlnode_insert_tag(node, "logging"),
	    sqldb_get_value(result, ndx_logging), -1);

	xmlnode_insert_cdata(xmlnode_insert_tag(node, "description"),
	    sqldb_get_value(result, ndx_description), -1);
    }

    if (tuples != 1){
	if (tuples == 0)
	    log_error(NULL,"[xdbsql_roomconfig_get] room not found");
	if (tuples > 1)
	    log_error(NULL,"[xdbsql_roomconfig_get] room not unique");
	sqldb_free_result(result);
	xmlnode_free(node);
	return NULL;
    }

    sqldb_free_result(result);

    return node;
}

static int
roomconfig_purge(XdbSqlDatas *self, const char *room){
    xmlnode query;	       /* the query for this function */
    query_def qd;	       /* the query definition */
    XdbSqlResult *result;      /* return code from query */

    /* Get the query definition. */
    query = xdbsql_query_get(self, "roomconfig-remove");

    if (!query){
	log_error(NULL,"--!!-- WTF? roomconfig-remove query not found?");
	return 0;
    }

    /* Build the query string. */
    qd = xdbsql_querydef_init(self, query);
    xdbsql_querydef_setvar(qd,"room",room);

    /* Execute the query! */
    result = sqldb_query(self, xdbsql_querydef_finalize(qd));
    xdbsql_querydef_free(qd);
    if (!result){
	log_error(NULL,"[roomconfig_purge] query failed");
	return 0;

    }
    sqldb_free_result(result);
    return 1;
}

int
xdbsql_roomconfig_set(XdbSqlDatas *self, const char *room, xmlnode data){
    xmlnode query;	       /* the query for this function */
    xmlnode x, x2;	       /* used to iterate through the rules */
    query_def qd;	       /* the query definition */
    XdbSqlResult *result;      /* return from query */

    char *data_owner = NULL;
    char *data_name = NULL;
    char *data_secret = NULL;
    char *data_private = NULL;
    char *data_leave = NULL;
    char *data_join = NULL;
    char *data_rename = NULL;
    char *data_public = NULL;
    char *data_subjectlock = NULL;
    char *data_maxusers = NULL;
    char *data_moderated = NULL;
    char *data_defaultype = NULL;
    char *data_privmsg = NULL;
    char *data_invitation = NULL;
    char *data_invites = NULL;
    char *data_legacy = NULL;
    char *data_visible = NULL;
    char *data_logformat = NULL;
    char *data_logging = NULL;
    char *data_description = NULL;
    char *data_persistant = NULL;

    const char *querystring;
    char *name;

    if (!room){
	/* the room was not specified - we have to bug off */
	log_error(NULL,"[xdbsql_roomconfig_set] room not specified");
	return 0;
    }

    if (!data){
	return roomconfig_purge(self, room);
    }

    //printf("FEMM: %s\n", xmlnode2str(data));

    /* Get the query definition. */
    query = xdbsql_query_get(self, "roomconfig-set");

    if (!query){
	log_error(NULL,"--!!-- WTF? roomconfig-set query not found?");
	return 0;
    }

    /* Purge any existing roomconfig data. */
    roomconfig_purge(self, room);

    //printf("FEMM: %s\n", xmlnode2str(data));

    for (x=xmlnode_get_firstchild(data); x; x=xmlnode_get_nextsibling(x)){

	/* Start to build the query string. */

	name = xmlnode_get_name(x);

	if (j_strcmp(name,"owner")==0){
	    data_owner = xmlnode_get_attrib(x, "jid");
	} else if (j_strcmp(name,"name")==0){
	    data_name = GET_CHILD_DATA(x);
	} else if (j_strcmp(name,"secret")==0){
	    data_secret = GET_CHILD_DATA(x);
	} else if (j_strcmp(name,"private")==0){
	    data_private = GET_CHILD_DATA(x);
	} else if (j_strcmp(name,"notice")==0){
	    for (x2=xmlnode_get_firstchild(x); x2; x2=xmlnode_get_nextsibling(x2)){
		name = xmlnode_get_name(x2);
		if (j_strcmp(name,"leave")==0){
		    data_leave = GET_CHILD_DATA(x2);
		} else if (j_strcmp(name,"join")==0){
		    data_join = GET_CHILD_DATA(x2);
		} else if (j_strcmp(name,"rename")==0){
		    data_rename = GET_CHILD_DATA(x2);
		}
	    }
	} else if (j_strcmp(name,"public")==0){
	    data_public = GET_CHILD_DATA(x);
	} else if (j_strcmp(name,"subjectlock")==0){
	    data_subjectlock = GET_CHILD_DATA(x);
	} else if (j_strcmp(name,"maxusers")==0){
	    data_maxusers = GET_CHILD_DATA(x);
	} else if (j_strcmp(name,"persistant")==0){
	    data_persistant = GET_CHILD_DATA(x);
	} else if (j_strcmp(name,"moderated")==0){
	    data_moderated = GET_CHILD_DATA(x);
	} else if (j_strcmp(name,"defaulttype")==0){
	    data_defaultype = GET_CHILD_DATA(x);
	} else if (j_strcmp(name,"privmsg")==0){
	    data_privmsg = GET_CHILD_DATA(x);
	} else if (j_strcmp(name,"invitation")==0){
	    data_invitation = GET_CHILD_DATA(x);
	} else if (j_strcmp(name,"invites")==0){
	    data_invites = GET_CHILD_DATA(x);
	} else if (j_strcmp(name,"legacy")==0){
	    data_legacy = GET_CHILD_DATA(x);
	} else if (j_strcmp(name,"visible")==0){
	    data_visible = GET_CHILD_DATA(x);
	} else if (j_strcmp(name,"logformat")==0){
	    data_logformat = GET_CHILD_DATA(x);
	} else if (j_strcmp(name,"logging")==0){
	    data_logging = GET_CHILD_DATA(x);
	} else if (j_strcmp(name,"description")==0){
	    data_description = GET_CHILD_DATA(x);
	}

    }

    qd = xdbsql_querydef_init(self, query);
    xdbsql_querydef_setvar(qd,"room",room);

    /* Fill in the rest of the query parameters. */
    if (data_owner && *data_owner)
      xdbsql_querydef_setvar(qd,"owner", data_owner );
    if (data_name && *data_name)
      xdbsql_querydef_setvar(qd,"name", data_name);
    if (data_secret && *data_secret)
      xdbsql_querydef_setvar(qd,"secret", data_secret);
    if (data_private && *data_private)
      xdbsql_querydef_setvar(qd,"private", data_private);
    if (data_leave && *data_leave)
      xdbsql_querydef_setvar(qd,"leave", data_leave);
    if (data_join && *data_join)
      xdbsql_querydef_setvar(qd,"join", data_join);
    if (data_rename && *data_rename)
      xdbsql_querydef_setvar(qd,"rename", data_rename);
    if (data_public && *data_public)
      xdbsql_querydef_setvar(qd,"public", data_public);
    if (data_subjectlock && *data_subjectlock)
      xdbsql_querydef_setvar(qd,"subjectlock", data_subjectlock);
    if (data_maxusers && *data_maxusers)
      xdbsql_querydef_setvar(qd,"maxusers", data_maxusers);
    if (data_moderated && *data_moderated)
      xdbsql_querydef_setvar(qd,"moderated", data_moderated);
    if (data_defaultype && *data_defaultype)
      xdbsql_querydef_setvar(qd,"defaultype", data_defaultype);
    if (data_privmsg && *data_privmsg)
      xdbsql_querydef_setvar(qd,"privmsg", data_privmsg);
    if (data_invitation && *data_invitation)
      xdbsql_querydef_setvar(qd,"invitation", data_invitation);
    if (data_invites && *data_invites)
      xdbsql_querydef_setvar(qd,"invites", data_invites);
    if (data_legacy && *data_legacy)
      xdbsql_querydef_setvar(qd,"legacy", data_legacy);
    if (data_visible && *data_visible)
      xdbsql_querydef_setvar(qd,"visible", data_visible);
    if (data_logformat && *data_logformat)
      xdbsql_querydef_setvar(qd,"logformat", data_logformat);
    if (data_logging && *data_logging)
      xdbsql_querydef_setvar(qd,"logging", data_logging);
    if (data_description && *data_description)
      xdbsql_querydef_setvar(qd,"description", data_description);
    if (data_persistant && *data_persistant)
      xdbsql_querydef_setvar(qd,"persistant", data_persistant);

    /* Execute the query! */
    querystring = xdbsql_querydef_finalize(qd);

    result = sqldb_query(self,querystring);

    xdbsql_querydef_free(qd);

    if (!result)
      log_error(NULL,"[xdbsql_roomconfig_set] query failed : %s",
		sqldb_error(self));
    else
      sqldb_free_result(result);

    return 1;
}
