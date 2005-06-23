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
#ifndef XDB_SQL_BACKEND_H_INCLUDED
#define XDB_SQL_BACKEND_H_INCLUDED

#include "xdb_sql.h"

struct XdbSqlBackend;

/*
 * Destroy a backend, freeing its ressources (memory, connection).
 */
typedef void (*XdbSqlDBDestroy)(struct XdbSqlBackend *self);

/*
 * Connect to a database.
 */
typedef int (*XdbSqlDBConnect)(struct XdbSqlBackend *self, const char *host,
			       const char *port, const char *user,
			       const char *pass, const char *db);
/*
 * Disconnect from database.
 */
typedef void (*XdbSqlDBDisconnect)(struct XdbSqlBackend *self);

/*
 * Return true if connected.
 */
typedef short (*XdbSqlDBIsConnected)(struct XdbSqlBackend *self);

/*
 * Send a SQL query and get result.
 */
typedef struct XdbSqlResult *(*XdbSqlDBQuery)(struct XdbSqlBackend *self,
					      const char *query);

/*
 * Get error string after a failed operation.
 */
typedef const char *(*XdbSqlDBError)(struct XdbSqlBackend *self);

/*
 * Use sequentially all tuples from a result, do not read entire
 * data set at once. Must read all the rows and not send
 * another query.
 */
typedef short (*XdbSqlDBUseResult)(struct XdbSqlResult *res);

/*
 * Store all tuples.
 */
typedef short (*XdbSqlDBStoreResult)(struct XdbSqlResult *res);

/*
 * Free result from a query.
 */
typedef void (*XdbSqlDBFreeResult)(struct XdbSqlResult *res);

/*
 * Returns number of tuples affected by last query, and -1
 * on error.
 */
typedef int (*XdbSqlDBNumTuples)(struct XdbSqlResult *res);

/*
 * Returns number of fields in the result, and -1
 * on error. 0 if it was an update query.
 */
typedef int (*XdbSqlDBNumFields)(struct XdbSqlResult *res);

/*
 * Advance to the next tuple.
 */
typedef short (*XdbSqlDBNextTuple)(struct XdbSqlResult *res);

/*
 * Get a field value from the current row.
 */
typedef const char * (*XdbSqlDBGetValue)(struct XdbSqlResult *res, int index);

/*
 * SQL-escape a string
 */
typedef int (*XdbSqlDBEscapeString)(struct XdbSqlBackend *self,
				    char *to, const char *from, int length);


/*
 * Little DB abstraction layer. This vtable is meant to be
 * included as first member in the "derived" instance structure
 * for each DB. Traditionnaly it's a pointer to vtable that is
 * included in each instance data, but I didn't see the real
 * interest here (there's only one "object" per "class").
 * You have one db connection per backend, and one backend
 * per xdb_sql instance datas.
 */
typedef struct XdbSqlBackend
{
    XdbSqlDBDestroy	 destroy;
    XdbSqlDBConnect	 connect;
    XdbSqlDBDisconnect	 disconnect;
    XdbSqlDBIsConnected  is_connected;
    XdbSqlDBQuery	 query;
    XdbSqlDBError	 error;
    XdbSqlDBUseResult	 use_result;
    XdbSqlDBStoreResult  store_result;
    XdbSqlDBFreeResult	 free_result;
    XdbSqlDBNumTuples	 num_tuples;
    XdbSqlDBNumFields	 num_fields;
    XdbSqlDBNextTuple	 next_tuple;
    XdbSqlDBGetValue	 get_value;
    XdbSqlDBEscapeString escape_string;
    /* datas here */
} XdbSqlBackend;


#endif /* XDB_SQL_BACKEND_H_INCLUDED */

