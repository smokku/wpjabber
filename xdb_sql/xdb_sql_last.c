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

static int
last_purge (XdbSqlDatas * self, const char *username){
  xmlnode query1;		/* query nodes */
  query_def qd;			/* the query definition */
  XdbSqlResult *result;		/* return code from query */

  /*
   * Get the query definitions
   */
  query1 = xdbsql_query_get (self, "last-purge");

  if (!query1){				/* no query - eep! */
      log_error (NULL, "--!!-- WTF? last-purge query not found?");
      return 0;

    } /* end if */

  /*
   * Build the query string.
   */
  qd = xdbsql_querydef_init (self, query1);
  xdbsql_querydef_setvar (qd, "user", username);

  /*
   * Execute the query!
   */
  result = sqldb_query (self, xdbsql_querydef_finalize (qd));
  xdbsql_querydef_free (qd);
  if (!result){				/* the query failed - bail out! */
      log_error (NULL, "[last_purge] query<1> failed : %s",
		 sqldb_error (self));
      return 0;

    } /* end if */

  sqldb_free_result (result);

  return 1;			/* Qa'pla! */

}				/* end last_purge */

xmlnode
xdbsql_last_get (XdbSqlDatas * self, const char *username){
  xmlnode rc = NULL;		/* return from this function */
  xmlnode query1;		/* query nodes */
  query_def qd;			/* the query definition */
  XdbSqlResult *result;		/* pointer to database result */
  int ndx_username, ndx_seconds, ndx_state;

  const char *tmp_attr;		/* temporary attribute value */

  if (!username){				/* the username was not specified - we have to
				 * bug off */
      log_error (NULL, "[xdbsql_last_get] username not specified");
      return NULL;

    } /* end if */


  /*
   * Get the query definitions
    */
  query1 = xdbsql_query_get (self, "last-get");
  if (!query1){				/* no query - eep! */
      log_error (NULL, "--!!-- WTF? last-get query not found?");
      return NULL;

    } /* end if */


  /*
   * Build the query string.
   */
  qd = xdbsql_querydef_init (self, query1);
  xdbsql_querydef_setvar (qd, "user", username);

  /*
   * Execute the query!
   */
  result = sqldb_query (self, xdbsql_querydef_finalize (qd));
  xdbsql_querydef_free (qd);
  if (!result){				/* the query failed - bail out! */
      log_error (NULL, "[xdbsql_last_get] query failed : %s",
		 sqldb_error (self));
      return NULL;

    } /* end if */

  /*
   * get the results from the query
   */
  if (!sqldb_use_result (result)){				/* unable to retrieve the result */
      log_error (NULL, "[xdbsql_last_get] result fetch failed : %s",
		 sqldb_error (self));
      return NULL;

    } /* end if */

  /*
   * Initialize the return value.
   */
  rc = xmlnode_new_tag ("query");
  xmlnode_put_attrib (rc, "xmlns", NS_LAST);

  while (sqldb_next_tuple (result) != 0){				/* look for all last
				 * entries and add them to
				 * our output tree */
      /*
       * initialize the column mapping indexes
       */
      col_map map = xdbsql_colmap_init (query1);
      ndx_username = xdbsql_colmap_index (map, "user");
      ndx_seconds = xdbsql_colmap_index (map, "seconds");
      ndx_state = xdbsql_colmap_index (map, "state");
      xdbsql_colmap_free (map);

      /*
       * Insert a new item under the result.
       */

      xmlnode_put_attrib (rc, "last", sqldb_get_value (result, ndx_seconds));

      tmp_attr = sqldb_get_value (result, ndx_state);
      if (tmp_attr && *tmp_attr)
	xmlnode_insert_cdata (rc, tmp_attr, -1);
      tmp_attr = NULL;
    }

  sqldb_free_result (result);	/* all done with this query */

  return rc;			/* all done with this operation! */

}				/* end xdbsql_last_get */

static int
test_root_last_node (xmlnode root){
  if (j_strcmp (xmlnode_get_name (root), "query") != 0){				/* it's not an IQ query node */
      log_error (NULL, "[xdbsql_last_set] not a <query> node");
      return 0;

    } /* end if */

  if (j_strcmp (xmlnode_get_attrib (root, "xmlns"), NS_LAST) != 0){				/* it has not got the right namespace */
      log_error (NULL, "[xdbsql_last_set] not a jabber:iq:last node");
      return 0;

    } /* end if */

  return 1;			/* ship it */

}				/* end test_root_last_node */

int
xdbsql_last_set (XdbSqlDatas * self, const char *username, xmlnode last){
  xmlnode query1;		/* query nodes */

  query_def qd;			/* the query definition */
  XdbSqlResult *result;		/* return from query */
  const char *tmp_attr;		/* temporary for getting attributes */
  const char *tmp_data;		/* temporary for getting datas */


  if (!username){				/* username not specified */
      log_error (NULL, "[xdbsql_last_set] username not specified");
      return 0;

    } /* end if */

  /*
   * if (!roster) NULL means we're removing the data
   *
   *
   * return roster_purge(self, username);
   */
  if (!test_root_last_node (last))
    return 0;			/* error already reported */

  /*
   * Get the query definitions
   */
  query1 = xdbsql_query_get (self, "last-set");
  if (!query1){				/* no query - eep! */
      log_error (NULL, "--!!-- WTF? last-set query not found?");
      return 0;

    } /* end if */

  /*
   * Get rid of any existing last entries for the username.
   */
  (void) last_purge (self, username);

				/* create the query definition and  begin
				 * binding variables */
  qd = xdbsql_querydef_init (self, query1);
  xdbsql_querydef_setvar (qd, "user", username);

  tmp_attr = xmlnode_get_attrib (last, "last");
  if (tmp_attr && *tmp_attr)
    xdbsql_querydef_setvar (qd, "seconds", tmp_attr);

  tmp_data = xmlnode_get_data(last);
  if (tmp_data && *tmp_data)
    xdbsql_querydef_setvar (qd, "state", tmp_data);

  /*
   * Execute the query!
   */
  result = sqldb_query (self, xdbsql_querydef_finalize (qd));
  xdbsql_querydef_free (qd);
  if (!result){			/* the query failed - bail out! */
      log_warn (ZONE, "[xdbsql_last_set] query failure on header"
		" for %s : %s", username, sqldb_error (self));
    } /* end if */
  sqldb_free_result (result);


  return 1;			/* Qa'pla! */

} /* end xdbsql_roster_set */
