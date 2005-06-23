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
#include "xdb_sql_odbc.h"
#include <sql.h>
#include <sqlext.h>

/* Remove comment to get a log of all memory allocations in /tmp/alloc.log
#include "log_mem.h"*/



/*
 * ODBC backend
 */
typedef struct XdbODBCBackend {
	/* vtable */
	XdbSqlBackend base;
	/* datas */
	SQLHDBC dbc;
	SQLHENV env;
	int debug; /* FIXME: should move into the parent struct */
}
XdbODBCBackend;



/*
 * A tuple from the result
 */
typedef struct Tuple {
	SQLCHAR** fields;
	int field_count;
}
Tuple;

static void tuple_alloc_fields(Tuple*,int field_count);
static void tuple_get_data(Tuple*,SQLHSTMT stmt);
static void tuple_free_fields(Tuple*);

/* The initial size to use for a field.
The field will be expanded if the data is bigger. */
#define FIELD_INITIAL_SIZE 40




/*
 * ODBC query result
 */
typedef struct XdbODBCResult {
	XdbSqlResult base;
	SQLHSTMT     stmt;
	Tuple*	     tuples;
	int	     tuple_count;
	int	     tuple_internal_count;
	SQLSMALLINT  field_count;
	int	     row;
	short	     result_stored;
}
XdbODBCResult;

static void result_free_data(XdbODBCResult*);

/* Number of tuples to add when XdbODBCResult::tuples is full */
#define TUPLE_GRANULARITY 10



static void	       xdbodbc_backend_destroy (XdbODBCBackend *self);
static int	       xdbodbc_connect (XdbODBCBackend *self,
					const char *host, const char *port,
					const char *user, const char *pass,
					const char *db);
static short	       xdbodbc_is_connected (XdbODBCBackend *self);
static void	       xdbodbc_disconnect (XdbODBCBackend *self);
static XdbODBCResult*  xdbodbc_query (XdbODBCBackend *self, const char *query);
static const char*     xdbodbc_error (XdbODBCBackend *self);
static short	       xdbodbc_use_result (XdbODBCResult *res);
static short	       xdbodbc_store_result (XdbODBCResult *res);
static void	       xdbodbc_free_result (XdbODBCResult *res);
static int	       xdbodbc_num_tuples (XdbODBCResult *res);
static int	       xdbodbc_num_fields (XdbODBCResult *res);
static short	       xdbodbc_next_tuple (XdbODBCResult *res);
static const char*     xdbodbc_get_value (XdbODBCResult *res, int index);
static int	       xdbodbc_escape_string (XdbODBCBackend *self, char *to,
			   const char *from, int len);



/*
 * vtable
 */
static const XdbSqlBackend xdbodbc_base = {
    (XdbSqlDBDestroy)xdbodbc_backend_destroy,
    (XdbSqlDBConnect)xdbodbc_connect,
    (XdbSqlDBDisconnect)xdbodbc_disconnect,
    (XdbSqlDBIsConnected)xdbodbc_is_connected,
    (XdbSqlDBQuery)xdbodbc_query,
    (XdbSqlDBError)xdbodbc_error,
    (XdbSqlDBUseResult)xdbodbc_use_result,
    (XdbSqlDBStoreResult)xdbodbc_store_result,
    (XdbSqlDBFreeResult)xdbodbc_free_result,
    (XdbSqlDBNumTuples)xdbodbc_num_tuples,
    (XdbSqlDBNumFields)xdbodbc_num_fields,
    (XdbSqlDBNextTuple)xdbodbc_next_tuple,
    (XdbSqlDBGetValue)xdbodbc_get_value,
    (XdbSqlDBEscapeString)xdbodbc_escape_string,
};


/*
 * ODBC error handling
 */
static SQLCHAR s_odbc_message[8192];
static void store_odbc_error(SQLSMALLINT htype,SQLHANDLE hndl,const char* filename, int line);

// Macro to call store_odbc_error with the current filename and line number
#define STORE_ODBC_ERROR(htype,hndl) store_odbc_error((htype),(hndl),__FILE__,__LINE__)



XdbSqlBackend *xdbodbc_backend_new (void){
	XdbODBCBackend *back;

	back = (XdbODBCBackend *)malloc(sizeof(*back));
	if (!back)
		return NULL;
	/* init vtable */
	back->base = xdbodbc_base;

	/* init datas */
	back->env=NULL;
	back->dbc=NULL;

	/* no debug unless otherwise stated */
	back->debug = 0;
	return (XdbSqlBackend *)back;
}




/*
 * Release resources associated with a backend.
 */
void xdbodbc_backend_destroy (XdbODBCBackend *self){
	if (xdbodbc_is_connected(self))
		xdbodbc_disconnect(self);
	free(self);
}




/*
 * Connect to the database
 * We don't care about host and port,
 * since they are defined in the odbc.ini file
 */
int xdbodbc_connect (XdbODBCBackend *self, const char *host, const char *port,
		     const char *user, const char *pass, const char *db){
	SQLRETURN ret;

	if (port && (atoi(port) == 0))
		port = NULL;

	/* Create environment */
	ret=SQLAllocHandle(SQL_HANDLE_ENV,0,&(self->env) );
	if (!SQL_SUCCEEDED(ret)){
		log_error(ZONE, "Couldn't allocate environment\n");
		return 1;
	}

	SQLSetEnvAttr(self->env,SQL_ATTR_ODBC_VERSION,(void*)SQL_OV_ODBC3,0);

	/* Create connection */
	ret=SQLAllocHandle(SQL_HANDLE_DBC,self->env,&(self->dbc) );
	if (!SQL_SUCCEEDED(ret)){
		log_error(ZONE, "Couldn't allocate connection\n");
		SQLFreeHandle(SQL_HANDLE_ENV,self->env);
		return 1;
	}

	/* Connect */
	ret=SQLConnect(self->dbc,(SQLCHAR*)db, (SWORD)strlen(db),
		   (SQLCHAR*)user, (SWORD)strlen(user),
		   (SQLCHAR*)pass, (SWORD)strlen(pass));
	if (!SQL_SUCCEEDED(ret)){
		STORE_ODBC_ERROR(SQL_HANDLE_DBC,self->dbc);
		SQLFreeHandle(SQL_HANDLE_DBC,self->dbc);
		SQLFreeHandle(SQL_HANDLE_ENV,self->env);
		return 1;
	}

	if (!xdbodbc_is_connected(self))
		return 0;

	return 1;
}




/*
 * True if connected to an odbc db.
 */
short xdbodbc_is_connected (XdbODBCBackend *self){
	static SQLCHAR buffer[255];
	SQLSMALLINT outlen ;
	SQLRETURN ret;

	if (!self->dbc)
		return 0;

	/* FIXME : There might be a faster way to do this */
	ret = SQLGetInfo(self->dbc,SQL_DATA_SOURCE_NAME, buffer,sizeof buffer,&outlen);

	if (!SQL_SUCCEEDED(ret))
		return 0;

	return 1;
}




/*
 * Disconnect from the db.
 */
void xdbodbc_disconnect (XdbODBCBackend *self){
	if (!xdbodbc_is_connected(self))
		return;
	SQLDisconnect(self->dbc);
	SQLFreeHandle(SQL_HANDLE_DBC,self->dbc);
	SQLFreeHandle(SQL_HANDLE_ENV,self->env);
	self->dbc=NULL;
	self->env=NULL;
}




/*
 * Issue a query to the db we are connected to.
 */
XdbODBCResult *xdbodbc_query (XdbODBCBackend *self, const char *query){
	SQLRETURN ret;
	XdbODBCResult* res;

	/* Create a XdbODBCResult */
	res=(XdbODBCResult*)malloc(sizeof(XdbODBCResult));
	if (!res){
		log_error(ZONE, "Couldn't allocate result\n");
		return NULL;
	}

	res->tuples=NULL;
	res->result_stored=0;
	_xdbsql_result_init(&(res->base), &(self->base));

	/* Allocate a statement */
	ret=SQLAllocHandle(SQL_HANDLE_STMT,self->dbc,&(res->stmt));
	if (!SQL_SUCCEEDED(ret)){
		STORE_ODBC_ERROR(SQL_HANDLE_DBC,self->dbc);
		free(res);
	}

	/* Use scrollable cursors */
	/* FIXME : Check if this is necessary */
	ret=SQLSetStmtAttr(res->stmt,
		SQL_ATTR_CURSOR_SCROLLABLE,
		(SQLPOINTER) SQL_SCROLLABLE,0);
	if (!SQL_SUCCEEDED(ret)){
		STORE_ODBC_ERROR(SQL_HANDLE_STMT,res->stmt);
		free(res);
	}

	/* Execute query */
    ret=SQLExecDirect(res->stmt,(SQLCHAR*)query,SQL_NTS);
	if (!SQL_SUCCEEDED(ret) && ret!=SQL_NO_DATA){
		log_error(ZONE, "Query failed with error code %d.\n%s",ret,query);
		STORE_ODBC_ERROR(SQL_HANDLE_STMT,res->stmt);
		SQLFreeHandle(SQL_HANDLE_STMT,res->stmt);
		free(res);
		return NULL;
	}

	return res;
}



/*
 * Returns error stored by store_odbc_error
 * We can't get the error directly because we need
 * all the ODBC params to get the error message.
 */
const char *xdbodbc_error (XdbODBCBackend *self){
	return s_odbc_message;
}




short xdbodbc_use_result (XdbODBCResult *res){
	return 1;
}




short xdbodbc_store_result (XdbODBCResult *res){
	SQLRETURN ret;

	if (!res){
		log_error(ZONE,"No result");
		return 0;
	}

	/* If already stored, get out */
	if (res->result_stored)
		return 1;

	/* Free any old tuples */
	if (res->tuples)
		result_free_data(res);

	/* Init data */
	res->tuple_count=0;
	res->tuple_internal_count=0;
	res->row=-1;

	/* Get field count */
    ret = SQLNumResultCols(res->stmt, &res->field_count);
	if (!SQL_SUCCEEDED(ret)){
		STORE_ODBC_ERROR(SQL_HANDLE_STMT,res->stmt);
		return 0;
	}

	/* Loop on tuples */
	while (	SQL_SUCCEEDED(SQLFetch(res->stmt)) ){

		/* No more tuple available, expand tuples */
		if (res->tuple_count==res->tuple_internal_count){

			res->tuples=(Tuple*)realloc(
				(void*)res->tuples,
				(res->tuple_internal_count+TUPLE_GRANULARITY)*sizeof(Tuple));

			if (!res->tuples){
				log_error(ZONE,"Could not expand tuples");
				result_free_data(res);
				return 0;
			}

			res->tuple_internal_count+=TUPLE_GRANULARITY;
		}

		/* Init a tuple */
		tuple_alloc_fields(&res->tuples[res->tuple_count],res->field_count);

		/* Get data into tuple */
		tuple_get_data(&res->tuples[res->tuple_count],res->stmt);
		++res->tuple_count;
	}

	/* Mark result as stored and return */
	res->result_stored=1;
	return 1;
}




/*
 * Free a result.
 */
void xdbodbc_free_result (XdbODBCResult *res){
	if (!res){
		log_error(ZONE,"No result");
		return;
	}

	result_free_data(res);

	SQLFreeHandle(SQL_HANDLE_STMT,res->stmt);
	free(res);
}




/*
 * Number of tuples in result, -1 if error
 */
int xdbodbc_num_tuples (XdbODBCResult *res){
	if (!xdbodbc_store_result(res))
		return -1;

	return res->tuple_count;
}




/*
 * Number of fields in result, -1 if error
 */
int xdbodbc_num_fields (XdbODBCResult *res){
	if (!xdbodbc_store_result(res))
		return -1;

	return res->field_count;
}



/*
 * Go to next tuple, 0 if error
 */
short xdbodbc_next_tuple (XdbODBCResult *res){
	/* Read data */
	if (!xdbodbc_store_result(res))
		return 0;

	/* EOF ? */
	if (res->row>=res->tuple_count-1)
		return 0;

	++res->row;
	return 1;
}




/*
 * Returns value of the field at given index in current tuple.
 */
const char *xdbodbc_get_value (XdbODBCResult *res, int index){
	if (!xdbodbc_store_result(res))
		return NULL;

	if (index<0 || index>=res->field_count){
		log_error(ZONE,"Index %d is not within 0 and %d",index,res->field_count-1);
		return NULL;
	}

	if (res->row>=res->tuple_count){
		log_error(ZONE,"Already at end of result");
		return NULL;
	}

	return res->tuples[res->row].fields[index];
}




int xdbodbc_escape_string (XdbODBCBackend *self, char *to, const char *from,
			   int len){
	unsigned char c;
	int i;

	for (i=0; i<len; i++){
		c = *from++;
		switch (c){
		case '\'':
			*to++ = '\'';
			*to++ = '\'';
			break;
		default:
			*to++ = c;
		}
	}
	*to = 0;
	return 1;
}




/********* xdb_sql_odbc support functions *********/
/*
 * Free all read data from a result
 */
static void result_free_data(XdbODBCResult* res){
	int pos;

	if (!res) return;
	if (!res->tuples) return;

	/* Free each tuple */
	for (pos=0;pos<res->tuple_count;++pos){
		tuple_free_fields(&res->tuples[pos]);
	}

	/* Free tuples */
	free(res->tuples);
	res->tuples=NULL;
	res->result_stored=0;
}



static void tuple_alloc_fields(Tuple* tuple,int field_count){
	int pos;

	tuple->field_count=field_count;
	tuple->fields=(SQLCHAR**)malloc(field_count*sizeof(SQLCHAR*));
	if (!tuple->fields){
		log_error(ZONE,"Could not allocate memory for tuple fields");
		return;
	}

	for(pos=0;pos<field_count;++pos){
		tuple->fields[pos]=NULL;
	}
}



/*#define LOCAL_DEBUG(x) x*/
#define LOCAL_DEBUG(x)
static void tuple_get_data(Tuple* tuple,SQLHSTMT stmt){
	SQLRETURN ret;
	SQLINTEGER read_len;
	int pos;

	for(pos=0;pos<tuple->field_count;++pos){
		LOCAL_DEBUG(printf("Field %d/%d\n",pos+1,tuple->field_count));

		/* Create a fixed length field */
		tuple->fields[pos]=(SQLCHAR*)malloc((FIELD_INITIAL_SIZE+1)*sizeof(SQLCHAR));

		/* Read data */
		ret=SQLGetData(stmt,
			pos+1,
			SQL_C_CHAR,
			tuple->fields[pos],
			FIELD_INITIAL_SIZE+1,
			&read_len);

		/* Read failed, get out */
		if (!SQL_SUCCEEDED(ret)){
			STORE_ODBC_ERROR(SQL_HANDLE_STMT,stmt);
			return;
		}

		/* We got all the data, proceed with next field */
		if (ret==SQL_SUCCESS){
			LOCAL_DEBUG(printf(" All read in one shot\n"));
			continue;
		}

		/* Field wasn't large enough; read_len contains total field length */
		LOCAL_DEBUG(printf(" Read %d bytes out of %d\n",FIELD_INITIAL_SIZE,read_len));

		/* Expand field */
		tuple->fields[pos]=(SQLCHAR*)realloc(
			(void*)tuple->fields[pos],
			(read_len+1)*sizeof(SQLCHAR));

		if (!tuple->fields[pos]){
			log_error(ZONE,"Couldn't expand field");
			return;
		}
		LOCAL_DEBUG(printf(" Expanded field\n"));

		/* Read remaining data */
		ret=SQLGetData(stmt,
			pos+1,
			SQL_C_CHAR,
			&(tuple->fields[pos][FIELD_INITIAL_SIZE]),
			read_len-FIELD_INITIAL_SIZE+1,
			&read_len);
		if (!SQL_SUCCEEDED(ret)){
			STORE_ODBC_ERROR(SQL_HANDLE_STMT,stmt);
			return;
		}
		LOCAL_DEBUG(printf(" Read %d bytes of remaining data\n",read_len));
	}
}



static void tuple_free_fields(Tuple* tuple){
	int pos;
	if (!tuple) return;

	if (tuple->fields){
		for(pos=0;pos<tuple->field_count;++pos){
			free(tuple->fields[pos]);
		}
	}
	free(tuple->fields);
}




/*
 * Store odbc error into s_odbc_message
 */
static void store_odbc_error(SQLSMALLINT htype,SQLHANDLE hndl,const char* filename,int line){
	SQLCHAR state[6], msg[SQL_MAX_MESSAGE_LENGTH];
	SQLINTEGER native_error;
	SQLSMALLINT pos,msg_len,total_len;



	for(pos=1;
		SQLGetDiagRec(htype, hndl, pos, state, &native_error,
			msg, sizeof(msg), &msg_len) != SQL_NO_DATA;
		++pos){
	/* Create "header" */
		if (pos==1){
			sprintf(s_odbc_message,
				"\n[ ODBC error in %s:%d - State : %s - Native error code : %d ]\n",
				filename,line,state,(int)native_error);
			total_len=strlen(s_odbc_message);
		}

	/* Add message while buffer is not full */
		if (total_len+msg_len>sizeof(s_odbc_message))
			return;
		total_len+=msg_len;
		strncat(s_odbc_message,msg,msg_len);
	}

	printf(s_odbc_message);
	printf("\n");
}



