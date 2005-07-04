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

/*
 * Handle generic backend stuff.
 */


#include "xdb_sql_backend.h"

#ifdef MYSQL_BACKEND
#include "xdb_sql_mysql.h"
#endif

#ifdef POSTGRESQL_BACKEND
#include "xdb_sql_pgsql.h"
#endif

#ifdef ODBC_BACKEND
#include "xdb_sql_odbc.h"
#endif

#ifdef ORACLE_BACKEND
#include "xdb_sql_oracle.h"
#endif

void _xdbsql_result_init(XdbSqlResult * res, XdbSqlBackend * backend)
{
	res->backend = backend;
}

int xdbsql_backend_load(XdbSqlDatas * self, const char *drvname)
{
#ifdef MYSQL_BACKEND
	if (!strcmp(drvname, "mysql")) {
		self->backend = xdbmysql_backend_new();
		return 1;
	}
#endif
#ifdef POSTGRESQL_BACKEND
	if (!strcmp(drvname, "pgsql") || !strncmp(drvname, "postgre", 7)) {
		self->backend = xdbpgsql_backend_new();
		return 1;
	}
#endif
#ifdef ODBC_BACKEND
	if (!strcmp(drvname, "odbc")) {
		self->backend = xdbodbc_backend_new();
		return 1;
	}
#endif
#ifdef ORACLE_BACKEND
	if (!strcmp(drvname, "oracle")) {
		self->backend = xdboracle_backend_new();
		return 1;
	}
#endif

	return 0;
}

int sqldb_connect(XdbSqlDatas * self, const char *host,
		  const char *port, const char *user, const char *pass,
		  const char *db)
{
	return (self->backend->connect) (self->backend, host, port,
					 user, pass, db);
}

void sqldb_disconnect(XdbSqlDatas * self)
{
	(self->backend->disconnect) (self->backend);
}

void xdbsql_backend_destroy(XdbSqlDatas * self)
{
	(self->backend->destroy) (self->backend);
}

short sqldb_is_connected(XdbSqlDatas * self)
{
	if (!self->backend)
		return 0;
	return (self->backend->is_connected) (self->backend);
}

/*
 * Return NULL if error
 */
XdbSqlResult *sqldb_query(XdbSqlDatas * self, const char *query)
{
	return (self->backend->query) (self->backend, query);
}

const char *sqldb_error(XdbSqlDatas * self)
{
	return (self->backend->error) (self->backend);
}

/*
 * Returns false if error
 */
short sqldb_use_result(XdbSqlResult * res)
{
	return (res->backend->use_result) (res);
}

/*
 * Returns false if error
 */
short sqldb_store_result(XdbSqlResult * res)
{
	return (res->backend->store_result) (res);
}

void sqldb_free_result(XdbSqlResult * res)
{
	if (res)
		(res->backend->free_result) (res);
}

/*
 * Number of tuples in result, -1 if error.
 */
int sqldb_num_tuples(XdbSqlResult * res)
{
	return (res->backend->num_tuples) (res);
}

/*
 * Number of fields in result, -1 if error.
 */
int sqldb_num_fields(XdbSqlResult * res)
{
	return (res->backend->num_fields) (res);
}

/*
 * Returns false if no next tuple.
 */
short sqldb_next_tuple(XdbSqlResult * res)
{
	return (res->backend->next_tuple) (res);
}

/*
 * Returns value of the field at given index in current tuple.
 */
const char *sqldb_get_value(XdbSqlResult * res, int index)
{
	return (res->backend->get_value) (res, index);
}

/*
 * SQL-escape a string.
 */
int sqldb_escape_string(XdbSqlDatas * self, char *to, const char *from,
			int len)
{
	return (self->backend->escape_string) (self->backend, to, from,
					       len);
}
