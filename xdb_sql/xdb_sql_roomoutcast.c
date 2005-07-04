/*
 * Author: Felipe Madero
 */

#include "xdb_sql.h"

#define GET_CHILD_DATA(X) xmlnode_get_data(xmlnode_get_firstchild(X));

xmlnode xdbsql_roomoutcast_get(XdbSqlDatas * self, const char *room)
{
	xmlnode node = NULL;	/* return from this function */
	xmlnode query;		/* query node */
	xmlnode element, reason;
	query_def qd;		/* the query definition */
	XdbSqlResult *result;	/* pointer to database result */
	int first = 1;

	int ndx_userid, ndx_reason, ndx_responsibleid, ndx_responsiblenick;

	if (!room) {
		/* the room was not specified - we have to bug off */
		log_error(NULL,
			  "[xdbsql_roomoutcast_get] room not specified");
		return NULL;
	}

	/* Get the query definitions */
	query = xdbsql_query_get(self, "roomoutcast-get");
	if (!query) {
		log_error(NULL,
			  "--!!-- WTF? roomoutcast-get query not found?");
		return NULL;

	}

	/* Load roomoutcast elements */

	/* Build the query string. */
	qd = xdbsql_querydef_init(self, query);
	xdbsql_querydef_setvar(qd, "room", room);

	/* Execute the query! */
	result = sqldb_query(self, xdbsql_querydef_finalize(qd));
	xdbsql_querydef_free(qd);
	if (!result) {
		log_error(NULL,
			  "[xdbsql_roomoutcast_get] query failed : %s",
			  sqldb_error(self));
		return NULL;
	}

	/* get the results from the query */
	if (!sqldb_use_result(result)) {
		log_error(NULL,
			  "[xdbsql_roomoutcast_get] result fetch failed : %s",
			  sqldb_error(self));
		return NULL;
	}

	node = xmlnode_new_tag("list");

	/* Initialize the return value. */
	while (sqldb_next_tuple(result) != 0) {
		/* look for all room entries and add them to our output tree */
		if (first) {
			/* initialize the column mapping indexes */
			col_map map = xdbsql_colmap_init(query);
			ndx_userid = xdbsql_colmap_index(map, "userid");
			ndx_reason = xdbsql_colmap_index(map, "reason");
			ndx_responsibleid =
			    xdbsql_colmap_index(map, "responsibleid");
			ndx_responsiblenick =
			    xdbsql_colmap_index(map, "responsiblenick");
			xdbsql_colmap_free(map);
			first = 0;
		}

		element = xmlnode_insert_tag(node, "item");
		xmlnode_put_attrib(element, "jid",
				   sqldb_get_value(result, ndx_userid));
		reason = xmlnode_insert_tag(element, "reason");
		xmlnode_insert_cdata(reason,
				     sqldb_get_value(result, ndx_reason),
				     -1);
		xmlnode_put_attrib(reason, "actor",
				   sqldb_get_value(result,
						   ndx_responsibleid));
		xmlnode_put_attrib(reason, "nick",
				   sqldb_get_value(result,
						   ndx_responsiblenick));

	}

	sqldb_free_result(result);

	return node;
}

static int roomoutcast_purge(XdbSqlDatas * self, const char *room)
{
	xmlnode query;		/* the query for this function */
	query_def qd;		/* the query definition */
	XdbSqlResult *result;	/* return code from query */

	/* Get the query definition. */
	query = xdbsql_query_get(self, "roomoutcast-remove");

	if (!query) {
		log_error(NULL,
			  "--!!-- WTF? roomoutcast-remove query not found?");
		return 0;
	}

	/* Build the query string. */
	qd = xdbsql_querydef_init(self, query);
	xdbsql_querydef_setvar(qd, "room", room);

	/* Execute the query! */
	result = sqldb_query(self, xdbsql_querydef_finalize(qd));
	xdbsql_querydef_free(qd);
	if (!result) {
		log_error(NULL, "[roomoutcast_purge] query failed");
		return 0;

	}
	sqldb_free_result(result);
	return 1;
}

int
xdbsql_roomoutcast_set(XdbSqlDatas * self, const char *room, xmlnode data)
{
	xmlnode query;		/* the query for this function */
	xmlnode x, x2;		/* used to iterate through the rules */
	query_def qd;		/* the query definition */
	XdbSqlResult *result;	/* return from query */

	char *data_userid = NULL;
	char *data_reason = NULL;
	char *data_responsibleid = NULL;
	char *data_responsiblenick = NULL;

	const char *querystring;
	char *name;

	if (!room) {
		/* the room was not specified - we have to bug off */
		log_error(NULL,
			  "[xdbsql_roomoutcast_set] room not specified");
		return 0;
	}

	if (!data) {
		return roomoutcast_purge(self, room);
	}

	/* Get the query definition. */
	query = xdbsql_query_get(self, "roomoutcast-set");
	if (!query) {
		log_error(NULL,
			  "--!!-- WTF? roomoutcast-set query not found?");
		return 0;
	}

	/* Purge any existing roomconfig data. */
	roomoutcast_purge(self, room);

	for (x = xmlnode_get_firstchild(data); x;
	     x = xmlnode_get_nextsibling(x)) {

		qd = xdbsql_querydef_init(self, query);
		xdbsql_querydef_setvar(qd, "room", room);

		data_userid = data_reason = data_responsibleid =
		    data_responsiblenick = NULL;

		name = xmlnode_get_name(x);
		if (j_strcmp(name, "item") == 0) {
			data_userid = xmlnode_get_attrib(x, "jid");
			for (x2 = xmlnode_get_firstchild(x); x2;
			     x2 = xmlnode_get_nextsibling(x2)) {
				name = xmlnode_get_name(x2);
				if (j_strcmp(name, "reason") == 0) {
					data_responsibleid =
					    xmlnode_get_attrib(x2,
							       "actor");
					data_responsiblenick =
					    xmlnode_get_attrib(x2, "nick");
					data_reason = GET_CHILD_DATA(x2);
				}
			}
		}

		if (data_userid && *data_userid)
			xdbsql_querydef_setvar(qd, "userid", data_userid);
		if (data_reason && *data_reason)
			xdbsql_querydef_setvar(qd, "reason", data_reason);
		if (data_responsibleid && *data_responsibleid)
			xdbsql_querydef_setvar(qd, "responsibleid",
					       data_responsibleid);
		if (data_responsiblenick && *data_responsiblenick)
			xdbsql_querydef_setvar(qd, "responsiblenick",
					       data_responsiblenick);

		querystring = xdbsql_querydef_finalize(qd);
		result = sqldb_query(self, querystring);
		xdbsql_querydef_free(qd);

		if (!result)
			log_error(NULL,
				  "[xdbsql_roomoutcast_set] query failed : %s",
				  sqldb_error(self));
		else
			sqldb_free_result(result);
	}

	return 1;
}
