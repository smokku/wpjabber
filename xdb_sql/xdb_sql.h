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
#ifndef XDB_SQL_H_INCLUDED
#define XDB_SQL_H_INCLUDED


#include <jabberd.h>

/*
 * Type definitions
 */

struct XdbSqlBackend;

typedef struct XdbSqlResult
{
    struct XdbSqlBackend *backend;
} XdbSqlResult;

struct XdbSqlDatas;

/* to store XML datas */
typedef int (*XdbSqlSetFunc)(struct XdbSqlDatas *self, const char *user, xmlnode datas);

/* to load XML datas */
typedef xmlnode (*XdbSqlGetFunc)(struct XdbSqlDatas *self, const char *user);

/* Associate a namespace with functions to set/get datas
 */
typedef struct XdbSqlModule
{
    const char *namespace;
    XdbSqlSetFunc set;
    XdbSqlGetFunc get;
} XdbSqlModule;

typedef struct query_node_struct *query_node;

/* passed to request handler */
/* query_table currently allocated in poolref - see config.c */
typedef struct XdbSqlDatas
{
    pool		  poolref; /* reference to the instance pool */
    struct XdbSqlBackend *backend; /* DB wrapper API */
    XdbSqlModule	 *modules; /* modules handled by this xdb */
    xmlnode		  config;  /* node from config file */
    struct query_table	 *query_table; /* for query validation */
    int			  sleep_debug; /* how much sleep before processing */
    query_node		  queries_v2;  /* queries with dtd v2 */
} XdbSqlDatas;

/* Query definition */
typedef struct query_def_struct *query_def;
typedef struct req_query_def_struct *req_query_def;

typedef struct query_def_bind_var_struct *query_def_bind_var;

/* Column map */
typedef struct col_map_struct *col_map;
typedef struct col_map_entry_struct *col_map_entry;
typedef enum { INS_TAG, INS_ATTRIB } BColInsType;

/* definition of the table that contains XML query definition references */
struct query_table
{
    const char *query_name;
    xmlnode query;
    int (*validate_func)(xmlnode q_root);
};

struct query_node_struct
{
    query_node	next;
    const char *query_name;
    query_def	qd;
};

/*
 * Function prototypes
 */

/* xdb_sql.c */
extern void xdb_sql(instance i, xmlnode x);

/* xdb_sql_backend.c */
void xdbsql_backend_destroy(XdbSqlDatas* self);
void _xdbsql_result_init (XdbSqlResult *res, struct XdbSqlBackend *backend);
int xdbsql_backend_load (XdbSqlDatas *self, const char *drvname);
int sqldb_connect (XdbSqlDatas *self, const char *host, const char *port,
		   const char *user, const char *pass, const char *db);
void sqldb_disconnect (XdbSqlDatas *self);
short sqldb_is_connected (XdbSqlDatas *self);
XdbSqlResult *sqldb_query (XdbSqlDatas *self, const char *query);
const char * sqldb_error (XdbSqlDatas *self);
short sqldb_use_result (XdbSqlResult *res);
short sqldb_store_result (XdbSqlResult *res);
void sqldb_free_result (XdbSqlResult *res);
int sqldb_num_tuples (XdbSqlResult *res);
int sqldb_num_fields (XdbSqlResult *res);
short sqldb_next_tuple (XdbSqlResult *res);
const char *sqldb_get_value (XdbSqlResult *res, int index);
int sqldb_escape_string (XdbSqlDatas *self, char *to, const char *from,
			 int len);

/* xdb_sql_auth.c */
extern xmlnode xdbsql_auth_get(XdbSqlDatas *self, const char *user);
extern int xdbsql_auth_set(XdbSqlDatas *self, const char *user, xmlnode data);

/* xdb_sql_authhash.c */
extern xmlnode xdbsql_authhash_get(XdbSqlDatas *self, const char *user);
extern int xdbsql_authhash_set(XdbSqlDatas *self, const char *user, xmlnode data);

/* xdb_sql_auth0k.c */
extern xmlnode xdbsql_auth0k_get(XdbSqlDatas *self, const char *user);
extern int xdbsql_auth0k_set(XdbSqlDatas *self, const char *user, xmlnode data);

/* xdb_sql_config.c */
extern int is_true(const char *str);
extern int is_false(const char *str);
extern int xdbsql_config_init(XdbSqlDatas *self, xmlnode cfgroot);
extern xmlnode xdbsql_query_get(XdbSqlDatas *self, const char *name);
extern void xdbsql_config_uninit(XdbSqlDatas *self);

/* xdb_sql_vcard.c */
extern xmlnode xdbsql_vcard_get(XdbSqlDatas *self, const char *user);
extern int xdbsql_vcard_set(XdbSqlDatas *self, const char *user, xmlnode data);

/* xdb_sql_filter.c */
extern xmlnode xdbsql_filter_get(XdbSqlDatas *self, const char *user);
extern int xdbsql_filter_set(XdbSqlDatas *self, const char *user, xmlnode data);

/* xdb_sql_roomconfig.c */
extern xmlnode xdbsql_roomconfig_get(XdbSqlDatas *self, const char *room);
extern int xdbsql_roomconfig_set(XdbSqlDatas *self, const char *room, xmlnode data);

/* xdb_sql_roomoutcast.c */
extern xmlnode xdbsql_roomoutcast_get(XdbSqlDatas *self, const char *room);
extern int xdbsql_roomoutcast_set(XdbSqlDatas *self, const char *room, xmlnode data);

/* xdb_sql_generic.c */
extern xmlnode xdbsql_simple_get(XdbSqlDatas *self, query_def qd, const char *user);
extern int xdbsql_simple_set (XdbSqlDatas *self, query_def qd,
			      const char *user, xmlnode datas);

/*  xdb_sql_mood.c */
extern xmlnode xdbsql_mood_get(XdbSqlDatas *self, const char *user);
extern int xdbsql_mood_set(XdbSqlDatas *self, const char *user, xmlnode data);

/* xdb_sql_offline.c */
extern xmlnode xdbsql_offline_get(XdbSqlDatas *self, const char *user);
extern int xdbsql_offline_set(XdbSqlDatas *self, const char *user, xmlnode data);

/* xdb_sql_querydef.c */
extern query_def xdbsql_querydef_init(XdbSqlDatas *self, xmlnode qd_root);
extern void xdbsql_querydef_setvar(query_def qd,
				   const char *name, const char *value);
extern char *xdbsql_querydef_finalize(query_def qd);
extern void xdbsql_querydef_free(query_def qd);
extern col_map xdbsql_colmap_init(xmlnode qd_root);
extern void xdbsql_colmap_free(col_map cm);
extern int xdbsql_colmap_index(col_map cm, const char *name);
extern query_def xdbsql_querydef_init_v2(XdbSqlDatas *self, pool p, xmlnode qd_root);
extern req_query_def xdbsql_reqquerydef_init(const query_def myqd, const char *user);
extern void xdbsql_reqquerydef_free(req_query_def rqd);
extern char *xdbsql_reqquerydef_finalize(req_query_def rqd);
extern xmlnode xdbsql_querydef_get_resnode(query_def qd);
extern xmlnode xdbsql_querydef_get_tuplenode(query_def qd);
extern col_map_entry xdbsql_querydef_get_firstcol(query_def qd);
extern col_map_entry xdbsql_querydef_get_nextcol(col_map_entry col);
extern const char *xdbsql_querydef_get_namespace (query_def qd);
extern const char *xdbsql_querydef_get_type (query_def qd);
const char *xdbsql_querydef_get_purge (query_def qd);
extern int xdbsql_querydef_col_get_offset (col_map_entry col);
extern const char *xdbsql_querydef_col_get_into (col_map_entry col);
extern const char *xdbsql_querydef_col_get_name (col_map_entry col);
extern BColInsType xdbsql_querydef_col_get_instype (col_map_entry col);
extern query_def_bind_var xdbsql_reqquerydef_get_firstvar (req_query_def rqd);
extern query_def_bind_var xdbsql_reqquerydef_get_nextvar (query_def_bind_var var);
extern const char *xdbsql_var_get_name (query_def_bind_var var);
extern int xdbsql_var_is_attrib (query_def_bind_var var);

/* xdb_sql_register.c */
extern xmlnode xdbsql_register_get(XdbSqlDatas *self, const char *user);
extern int xdbsql_register_set(XdbSqlDatas *self, const char *user, xmlnode data);

/* xdb_sql_roster.c */
extern xmlnode xdbsql_roster_get(XdbSqlDatas *self, const char *user);
extern int xdbsql_roster_set(XdbSqlDatas *self, const char *user, xmlnode roster);

/* xdb_sql_last.c */
extern xmlnode xdbsql_last_get(XdbSqlDatas *self, const char *user);
extern int xdbsql_last_set(XdbSqlDatas *self, const char *user, xmlnode roster);

#endif	/* XDB_SQL_H_INCLUDED */
