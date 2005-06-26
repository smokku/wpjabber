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
#include "xdb_sql_pgsql.h"

/*
 * Postgres backend
 */
typedef struct XdbPgsqlBackend
{
    /* vtable */
    XdbSqlBackend base;
    /* datas */
    PGconn *connection;
    int async_mode;
    int debug; /* FIXME: should move into the parent struct */
    SEM_VAR sem;
} XdbPgsqlBackend;


/*
 * Postgres query result
 */
typedef struct XdbPgsqlResult
{
    XdbSqlResult base;
    PGresult	*result;
    int		 row;
} XdbPgsqlResult;


static void	       xdbpgsql_backend_destroy (XdbPgsqlBackend *self);
static int	       xdbpgsql_connect (XdbPgsqlBackend *self,
					 const char *host, const char *port,
					 const char *user, const char *pass,
					 const char *db);
static short	       xdbpgsql_is_connected (XdbPgsqlBackend *self);
static void	       xdbpgsql_disconnect (XdbPgsqlBackend *self);
static XdbPgsqlResult *xdbpgsql_query (XdbPgsqlBackend *self, const char *query);
static const char     *xdbpgsql_error (XdbPgsqlBackend *self);
static short	       xdbpgsql_use_result (XdbPgsqlResult *res);
static short	       xdbpgsql_store_result (XdbPgsqlResult *res);
static void	       xdbpgsql_free_result (XdbPgsqlResult *res);
static int	       xdbpgsql_num_tuples (XdbPgsqlResult *res);
static int	       xdbpgsql_num_fields (XdbPgsqlResult *res);
static short	       xdbpgsql_next_tuple (XdbPgsqlResult *res);
static const char     *xdbpgsql_get_value (XdbPgsqlResult *res, int index);
static int	       xdbpgsql_escape_string (XdbPgsqlBackend *self, char *to,
					       const char *from, int len);

/*
 * Postgres vtable
 */
static const XdbSqlBackend xdbpgsql_base =
{
    (XdbSqlDBDestroy)xdbpgsql_backend_destroy,
    (XdbSqlDBConnect)xdbpgsql_connect,
    (XdbSqlDBDisconnect)xdbpgsql_disconnect,
    (XdbSqlDBIsConnected)xdbpgsql_is_connected,
    (XdbSqlDBQuery)xdbpgsql_query,
    (XdbSqlDBError)xdbpgsql_error,
    (XdbSqlDBUseResult)xdbpgsql_use_result,
    (XdbSqlDBStoreResult)xdbpgsql_store_result,
    (XdbSqlDBFreeResult)xdbpgsql_free_result,
    (XdbSqlDBNumTuples)xdbpgsql_num_tuples,
    (XdbSqlDBNumFields)xdbpgsql_num_fields,
    (XdbSqlDBNextTuple)xdbpgsql_next_tuple,
    (XdbSqlDBGetValue)xdbpgsql_get_value,
    (XdbSqlDBEscapeString)xdbpgsql_escape_string,
};


static XdbPgsqlResult *result_new (XdbPgsqlBackend *self, PGresult *res);
static void result_destroy (XdbPgsqlResult *res);


XdbSqlBackend *xdbpgsql_backend_new (void){
    XdbPgsqlBackend *back;

    back = (XdbPgsqlBackend *)malloc(sizeof(*back));
    if (!back)
	return NULL;
    /* init vtable */
    back->base = xdbpgsql_base;
    /* init datas */
    back->connection = NULL;

    /* asynchronous operation mode by default */
    back->async_mode = 1;
    SEM_INIT(back->sem);

    /* no debug unless otherwise stated */
    back->debug = 0;
    return (XdbSqlBackend *)back;
}

/*
 * Release resources associated with a backend.
 */
void xdbpgsql_backend_destroy (XdbPgsqlBackend *self){
    if (xdbpgsql_is_connected(self))
	xdbpgsql_disconnect(self);
    free(self);
}

/*
 * Connect to the database.
 */
int xdbpgsql_connect (XdbPgsqlBackend *self, const char *host, const char *port,
		      const char *user, const char *pass, const char *db){
    if (port && (atoi(port) == 0))
	port = NULL;
    self->connection = PQsetdbLogin(host, port, NULL, NULL,
				    db, user, pass);

    if (self->async_mode)
	PQsetnonblocking(self->connection, 1);

    if (!xdbpgsql_is_connected(self))
	return 0;

    return 1;
}

/*
 * True if connected to a pgsql db.
 */
short xdbpgsql_is_connected (XdbPgsqlBackend *self){
    if (!self->connection || PQstatus(self->connection) != CONNECTION_OK)
	return 0;
    return 1;
}


/*
 * Disconnect from the pgsql db.
 */
void xdbpgsql_disconnect (XdbPgsqlBackend *self){
    if (!xdbpgsql_is_connected(self))
	return;
    PQfinish(self->connection);
    self->connection = NULL;
}

/*
 * Issue a query to the pgsql db we are connected to.
 */
XdbPgsqlResult *xdbpgsql_query (XdbPgsqlBackend *self, const char *query){
    PGresult	*r;
    XdbPgsqlResult *res;
    ExecStatusType st;
    int i;

    /* let's see wat the actual query is */
    log_debug("PSQL query: %s", query);
    
    if (!self->async_mode){
	/* synchronous mode */
	r = PQexec(self->connection, query);
	if (!r)
	    return NULL;

    } else{
	/* async mode */
	SEM_LOCK(self->sem);
	i = PQsendQuery(self->connection, query);

	if (i == 0)
	    log_error(ZONE, "xdbpgsql_query: ERROR %s\n", PQerrorMessage(self->connection));

	while (1){
	    i = PQconsumeInput(self->connection);
	    if (PQisBusy(self->connection))
		usleep(15);
	    else
		break;
	}

	r = PQgetResult(self->connection);
	if (!r)
	    return NULL;

	while (PQgetResult(self->connection) != NULL)
	  usleep(15);
    }

    st = PQresultStatus(r);

    if (st != PGRES_COMMAND_OK && st !=PGRES_TUPLES_OK){
	log_error(ZONE, "error: r=%p [%s]\n", r, PQresultErrorMessage(r));
	PQclear(r);
	if (self->async_mode)
	    SEM_UNLOCK(self->sem);
	return NULL;
    }

    res = result_new(self, r);
    if (!res){
	PQclear(r);
	if (self->async_mode)
	    SEM_UNLOCK(self->sem);
	return NULL;
    }

    if (self->async_mode)
	SEM_UNLOCK(self->sem);
    return res;
}

const char *xdbpgsql_error (XdbPgsqlBackend *self){
    return PQerrorMessage(self->connection);
}

short xdbpgsql_use_result (XdbPgsqlResult *res){
    return 1;
}

short xdbpgsql_store_result (XdbPgsqlResult *res){
    return 1;
}

/*
 * Free a result.
 */
void xdbpgsql_free_result (XdbPgsqlResult *res){
    result_destroy(res);
}

/*
 * Number of tuples in result, -1 if error.
 */
int xdbpgsql_num_tuples (XdbPgsqlResult *res){
    if (!res->result)
	return -1;
    return PQntuples(res->result);
}

/*
 * Number of fields in result, -1 if error.
 */
int xdbpgsql_num_fields (XdbPgsqlResult *res){
    if (!res->result)
	return -1;
    return PQnfields(res->result);
}

short xdbpgsql_next_tuple (XdbPgsqlResult *res){
    if (!res->result)
	return 0;
    res->row++;
    if (res->row < PQntuples(res->result)){
	return 1;
    }
    else{
	return 0;
    }
}

/*
 * Returns value of the field at given index in current tuple.
 */
const char *xdbpgsql_get_value (XdbPgsqlResult *res, int index){
    if (!res->result)
	return NULL;
    if (index < 0 || index >= PQnfields(res->result))
	return NULL;
    if (res->row < 0)
	return NULL;
    return PQgetvalue(res->result, res->row, index);
}

int xdbpgsql_escape_string (XdbPgsqlBackend *self, char *to, const char *from,
			    int len){
    unsigned char c;
    int i;

    for (i = 0; i < len; i++){
	c = *from++;
	switch (c){
	    case 0:
		*to++ = '\\';
		*to++ = '0';
		break;
	    case '\n':
		*to++ = '\\';
		*to++ = 'n';
		break;
	    case '\t':
		*to++ = '\\';
		*to++ = 't';
		break;
	    case '\b':
		*to++ = '\\';
		*to++ = 'b';
		break;
	    case '\r':
		*to++ = '\\';
		*to++ = 'r';
		break;
	    case '\'':
		*to++ = '\\';
		*to++ = '\'';
		break;
	    case '\"':
		*to++ = '\\';
		*to++ = '"';
		break;
	    case '%':
		*to++ = '\\';
		*to++ = '%';
		break;
	    case '_':
		*to++ = '\\';
		*to++ = '_';
		break;
	    case '\\':
		*to++ = '\\';
		*to++ = '\\';
		break;
	    default:
		*to++ = c;
	}
    }
    *to = 0;
    return 1;
}


/********* xdb_sql_pgsql support functions *********/

/*
 * Create a new XdbPgsqlResult.
 */
static XdbPgsqlResult *result_new (XdbPgsqlBackend *self, PGresult *r){
    XdbPgsqlResult *res;

    res = (XdbPgsqlResult *)malloc(sizeof(*res));
    if (!res){
	return NULL;
    }
    _xdbsql_result_init(&(res->base), &(self->base));
    res->result = r;
    res->row = -1;
    return res;
}

/*
 * Destroy a XdbPgsqlResult.
 */
static void result_destroy (XdbPgsqlResult *res){
    if (res){
	if (res->result)
	    PQclear(res->result);

	free(res);
    }
}



