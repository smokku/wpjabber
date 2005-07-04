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
#include "xdb_sql_mysql.h"

/*
 * There probably should be soft asserts, using g_return_xxx
 */

/*
 * MySQL backend
 */
typedef struct XdbMysqlBackend {
	/* vtable */
	XdbSqlBackend base;
	/* datas */
	MYSQL mysql;
	MYSQL *connection;
} XdbMysqlBackend;

/*
 * MySQL query result
 */
typedef struct XdbMysqlResult {
	XdbSqlResult base;
	short stored;
	MYSQL_RES *result;
	MYSQL_ROW row;
} XdbMysqlResult;

/*
 * MySQL vtable functions
 */
static void xdbmysql_backend_destroy(XdbMysqlBackend * self);
static int xdbmysql_connect(XdbMysqlBackend * self,
			    const char *host, const char *port,
			    const char *user, const char *pass,
			    const char *db);
static void xdbmysql_disconnect(XdbMysqlBackend * self);
static short xdbmysql_is_connected(XdbMysqlBackend * self);
static XdbMysqlResult *xdbmysql_query(XdbMysqlBackend * self,
				      const char *query);
static const char *xdbmysql_error(XdbMysqlBackend * self);
static short xdbmysql_use_result(XdbMysqlResult * res);
static short xdbmysql_store_result(XdbMysqlResult * res);
static void xdbmysql_free_result(XdbMysqlResult * res);
static int xdbmysql_num_tuples(XdbMysqlResult * res);
static int xdbmysql_num_fields(XdbMysqlResult * res);
static short xdbmysql_next_tuple(XdbMysqlResult * res);
static const char *xdbmysql_get_value(XdbMysqlResult * res, int index);
static int xdbmysql_escape_string(XdbMysqlBackend * self, char *to,
				  const char *from, int len);

/*
 * Support functions.
 */
static XdbMysqlResult *result_new(XdbMysqlBackend * self);
static void result_destroy(XdbMysqlResult * res);
static XdbMysqlBackend *result_get_backend(XdbMysqlResult * res);


/*
 * MySQL vtable
 */
static const XdbSqlBackend xdbmysql_base = {
	(XdbSqlDBDestroy) xdbmysql_backend_destroy,
	(XdbSqlDBConnect) xdbmysql_connect,
	(XdbSqlDBDisconnect) xdbmysql_disconnect,
	(XdbSqlDBIsConnected) xdbmysql_is_connected,
	(XdbSqlDBQuery) xdbmysql_query,
	(XdbSqlDBError) xdbmysql_error,
	(XdbSqlDBUseResult) xdbmysql_use_result,
	(XdbSqlDBStoreResult) xdbmysql_store_result,
	(XdbSqlDBFreeResult) xdbmysql_free_result,
	(XdbSqlDBNumTuples) xdbmysql_num_tuples,
	(XdbSqlDBNumFields) xdbmysql_next_tuple,
	(XdbSqlDBNextTuple) xdbmysql_next_tuple,
	(XdbSqlDBGetValue) xdbmysql_get_value,
	(XdbSqlDBEscapeString) xdbmysql_escape_string,
};

/*
 * Create a new MySQL backend.
 */
XdbSqlBackend *xdbmysql_backend_new(void)
{
	XdbMysqlBackend *back;

	back = (XdbMysqlBackend *) malloc(sizeof(*back));
	if (!back)
		return NULL;
	/* init vtable */
	back->base = xdbmysql_base;
	/* init datas */
	back->connection = NULL;
	return (XdbSqlBackend *) back;
}

/*
 * Release resources associated with a backend.
 */
void xdbmysql_backend_destroy(XdbMysqlBackend * self)
{
	if (xdbmysql_is_connected(self))
		xdbmysql_disconnect(self);
	free(self);
}

/*
 * Connect to the database.
 */
int xdbmysql_connect(XdbMysqlBackend * self, const char *host,
		     const char *port, const char *user, const char *pass,
		     const char *db)
{
	int nport;

	if (!port || sscanf(port, "%d", &nport) < 1)
		nport = 0;

	mysql_init(&(self->mysql));
	self->connection =
	    mysql_real_connect(&(self->mysql), host, user, pass, db, nport,
			       NULL, 0);
	if (!xdbmysql_is_connected(self))
		return 0;

	return 1;
}

/*
 * True if connected to a mysql db.
 */
short xdbmysql_is_connected(XdbMysqlBackend * self)
{
	return self->connection != NULL;
}

/*
 * Disconnect from the mysql db.
 */
void xdbmysql_disconnect(XdbMysqlBackend * self)
{
	if (!xdbmysql_is_connected(self))
		return;
	mysql_close(self->connection);
	self->connection = NULL;
}

/*
 * Issue a query to the mysql db we are connected to.
 */
XdbMysqlResult *xdbmysql_query(XdbMysqlBackend * self, const char *query)
{
	int state;
	XdbMysqlResult *res;

	state = mysql_query(self->connection, query);
	if (state) {
		return NULL;
	}

	res = result_new(self);
	if (!res) {
		return NULL;
	}

	return res;
}

/*
 * Get latest error string.
 */
const char *xdbmysql_error(XdbMysqlBackend * self)
{
	if (xdbmysql_is_connected(self))
		return mysql_error(self->connection);
	else
		return mysql_error(&(self->mysql));
}

/*
 * Process one tuple at once.
 */
short xdbmysql_use_result(XdbMysqlResult * res)
{
	res->result =
	    mysql_use_result(result_get_backend(res)->connection);
	if (!res->result)
		return 0;
	return 1;
}

/*
 * Retrieve entire result set.
 */
short xdbmysql_store_result(XdbMysqlResult * res)
{
	res->result =
	    mysql_store_result(result_get_backend(res)->connection);
	if (!res->result)
		return 0;
	res->stored = 1;
	return 1;
}

/*
 * Free a result.
 */
void xdbmysql_free_result(XdbMysqlResult * res)
{
	result_destroy(res);
}

/*
 * Number of tuples in result, -1 if error.
 */
int xdbmysql_num_tuples(XdbMysqlResult * res)
{
	if (!res->result || !res->stored)
		return -1;
	return mysql_num_rows(res->result);
}

/*
 * Number of fields in result, -1 if error.
 */
int xdbmysql_num_fields(XdbMysqlResult * res)
{
	if (!res->result)
		return -1;
	return mysql_num_fields(res->result);
}

/*
 * Advance to next tuple.
 */
short xdbmysql_next_tuple(XdbMysqlResult * res)
{
	if (!res->result)
		return 0;
	res->row = mysql_fetch_row(res->result);
	if (!res->row)
		return 0;
	return 1;
}

/*
 * Returns value of the field at given index in current tuple.
 */
const char *xdbmysql_get_value(XdbMysqlResult * res, int index)
{
	if (!res->result)
		return NULL;
	if (index < 0 || index >= xdbmysql_num_fields(res))
		return NULL;
	return res->row[index];
}

/*
 * Escape special characters. "len" is "from" size. "to" must point to
 * 2*len+1 chars.
 */
int xdbmysql_escape_string(XdbMysqlBackend * self, char *to,
			   const char *from, int len)
{
#ifdef HAS_MYSQL_REAL_ESCAPE_STRING
	return mysql_real_escape_string(self->connection, to, from, len);
#else
	return mysql_escape_string(to, from, len);
#endif
}

/********* xdb_sql_mysql support functions *********/

/*
 * Create a new XdbMysqlResult.
 */
static XdbMysqlResult *result_new(XdbMysqlBackend * self)
{
	XdbMysqlResult *res;

	res = (XdbMysqlResult *) malloc(sizeof(*res));
	if (!res)
		return NULL;

	_xdbsql_result_init(&(res->base), &(self->base));
	res->stored = 0;
	res->result = NULL;
	return res;
}

/*
 * Destroy a XdbMysqlResult.
 */
static void result_destroy(XdbMysqlResult * res)
{
	if (res) {
		if (res->result)
			mysql_free_result(res->result);
		free(res);
	}
}

/*
 * Get XdbMysqlBackend from a XdbMysqlResult.
 */
static XdbMysqlBackend *result_get_backend(XdbMysqlResult * res)
{
	struct XdbSqlBackend *b = res->base.backend;
	return (XdbMysqlBackend *) b;
}
