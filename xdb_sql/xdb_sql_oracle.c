/* --------------------------------------------------------------------------
*
*  Oracle support routines for xdb_sql
*
*  Developed by Mike Shoyher <mike@shoyher.com> http://www.shoyher.com
*
*  Usage, modification and distribution rights reserved. See file
*  COPYING for details.
*
* --------------------------------------------------------------------------*/


#include "xdb_sql_oracle.h"

/* Remove comment to get a log of all memory allocations in /tmp/alloc.log
#include "log_mem.h"*/


/* no more than 128 fields */
#define MAX_FIELDS 128
/* no more than 1000 rows */
#define MAX_TUPLES 1000
/* max size of a field	4000 - max for VARCHAR */
#define MAX_FIELD_SIZE 4000
/* Number of tuples to add when XdbODBCResult::tuples is full */
#define TUPLE_INCREMENT 10

/*
* Oracle backend
*/
typedef struct XdbOracleBackend {
	/* vtable */
	XdbSqlBackend base;
	/* Oracle specific */
	/* OCI handlers */
	OCIEnv *envhp;
	OCIError *errhp;
	OCISvcCtx *svchp;

	int debug;		/* FIXME: should move into the parent struct */
	char errbuf[512];
} XdbOracleBackend;



/*
* A tuple from the result
*/
typedef struct Tuple {
	char **fields;
	int field_count;
} Tuple;

static void tuple_alloc_fields(Tuple *, int field_count);
static void tuple_free_fields(Tuple *);
static void dprintf(char *format, ...);
static int checkerr(OCIError * errhp, sword status, int reporterr);

#ifdef DEBUG
#define DebugPrint   dprintf
#else
#define DebugPrint  1?0:dprintf
#endif


/*
* oracle query result
*/
typedef struct XdbOracleResult {
	XdbSqlResult base;
	/* Oracle specific */
	XdbOracleBackend *backend;
	OCIStmt *stmthp;
	Tuple *tuples;
	int tuple_count;
	int tuple_allocated;
	short field_count;
	int row;
	short result_stored;
	short field_size[MAX_FIELDS];
} XdbOracleResult;

XdbOracleResult *oracle_query(XdbOracleBackend * self, const char *query,
			      int reporterr);
static void result_free_data(XdbOracleResult *);




static void xdboracle_backend_destroy(XdbOracleBackend * self);
static int xdboracle_connect(XdbOracleBackend * self,
			     const char *host, const char *port,
			     const char *user, const char *pass,
			     const char *db);
static short xdboracle_is_connected(XdbOracleBackend * self);
static void xdboracle_disconnect(XdbOracleBackend * self);
static XdbOracleResult *xdboracle_query(XdbOracleBackend * self,
					const char *query);
static const char *xdboracle_error(XdbOracleBackend * self);
static short xdboracle_use_result(XdbOracleResult * res);
static short xdboracle_store_result(XdbOracleResult * res);
static void xdboracle_free_result(XdbOracleResult * res);
static int xdboracle_num_tuples(XdbOracleResult * res);
static int xdboracle_num_fields(XdbOracleResult * res);
static short xdboracle_next_tuple(XdbOracleResult * res);
static const char *xdboracle_get_value(XdbOracleResult * res, int index);
static int xdboracle_escape_string(XdbOracleBackend * self, char *to,
				   const char *from, int len);



						/*
						 * vtable
						 */
static const XdbSqlBackend xdboracle_base = {
	(XdbSqlDBDestroy) xdboracle_backend_destroy,
	(XdbSqlDBConnect) xdboracle_connect,
	(XdbSqlDBDisconnect) xdboracle_disconnect,
	(XdbSqlDBIsConnected) xdboracle_is_connected,
	(XdbSqlDBQuery) xdboracle_query,
	(XdbSqlDBError) xdboracle_error,
	(XdbSqlDBUseResult) xdboracle_use_result,
	(XdbSqlDBStoreResult) xdboracle_store_result,
	(XdbSqlDBFreeResult) xdboracle_free_result,
	(XdbSqlDBNumTuples) xdboracle_num_tuples,
	(XdbSqlDBNumFields) xdboracle_num_fields,
	(XdbSqlDBNextTuple) xdboracle_next_tuple,
	(XdbSqlDBGetValue) xdboracle_get_value,
	(XdbSqlDBEscapeString) xdboracle_escape_string,
};


XdbSqlBackend *xdboracle_backend_new(void)
{
	XdbOracleBackend *back;

	back = (XdbOracleBackend *) malloc(sizeof(*back));
	bzero(back, sizeof(*back));
	if (!back)
		return NULL;
	/* init vtable */
	back->base = xdboracle_base;

	/* init datas */
	/* Allocate and initialize OCI environment handle, envhp */
	if (checkerr
	    (back->errhp,
	     OCIInitialize((ub4) OCI_OBJECT, NULL, NULL, NULL, NULL), 1)) {
		free(back);
		return NULL;
	}
	if (checkerr
	    (back->errhp,
	     OCIEnvInit(&back->envhp, (ub4) OCI_DEFAULT, (size_t) 0,
			(dvoid **) 0), 1)) {
		free(back);
		return NULL;
	}
	if (checkerr
	    (back->errhp,
	     OCIHandleAlloc(back->envhp, (void **) &back->errhp,
			    OCI_HTYPE_ERROR, (size_t) 0, (dvoid **) 0),
	     1)) {
		OCIHandleFree((dvoid *) back->envhp, OCI_HTYPE_ENV);
		free(back);
		return NULL;
	}

	/* no debug unless otherwise stated */
	back->debug = 0;
	DebugPrint("xdboracle_backend_new: initialized");
	return (XdbSqlBackend *) back;
}




/*
* Release resources associated with a backend.
*/
void xdboracle_backend_destroy(XdbOracleBackend * self)
{
	if (xdboracle_is_connected(self))
		xdboracle_disconnect(self);
	OCIHandleFree((dvoid *) self->errhp, OCI_HTYPE_ERROR);
	OCIHandleFree((dvoid *) self->envhp, OCI_HTYPE_ENV);
	free(self);
}




/*
* Connect to the database
*/
int xdboracle_connect(XdbOracleBackend * self, const char *host,
		      const char *port, const char *user, const char *pass,
		      const char *db)
{
	/* Connect */
	if (checkerr(self->errhp, OCILogon(self->envhp, self->errhp,
					   &self->svchp, (text *) user,
					   (ub4) strlen(user),
					   (text *) pass,
					   (ub4) strlen(pass), (text *) db,
					   (ub4) strlen(db)), 1)) {
		return 0;
	}
	DebugPrint
	    ("xdboracle_connect: Logged to Oracle, db=%s, user=%s, pass=%s",
	     db, user, pass);
	if (!xdboracle_is_connected(self)) {
		OCILogoff(self->svchp, self->errhp);
		OCIHandleFree((dvoid *) self->svchp,
			      (ub4) OCI_HTYPE_SVCCTX);
		return 0;
	}
	return 1;
}



/*
* True if connected to an oracle db.
*/
short xdboracle_is_connected(XdbOracleBackend * self)
{
	/* check connection */
	XdbOracleResult *res;
	/* Pretty stupid way to check if Oracle is alive, copied from DBD::Oracle */
	res = oracle_query(self, "select SYSDATE from DUAL", 0);
#if DEBUG
	if (res) {
		DebugPrint("xdboracle_is_connected: connected");
	} else {
		DebugPrint("xdboracle_is_connected: not connected");
	}
#endif
	if (!res)
		return 0;
	xdboracle_free_result(res);
	return 1;
}




/*
* Disconnect from the db.
*/
void xdboracle_disconnect(XdbOracleBackend * self)
{
	if (!xdboracle_is_connected(self)) {
		DebugPrint("xdboracle_disconnect: not connected");
		return;
	}
	OCILogoff(self->svchp, self->errhp);
	OCIHandleFree((dvoid *) self->svchp, (ub4) OCI_HTYPE_SVCCTX);
	self->svchp = NULL;
	DebugPrint("xdboracle_disconnect: disconnected");
}



XdbOracleResult *oracle_query(XdbOracleBackend * self, const char *query,
			      int reporterr)
{

	XdbOracleResult *res;
	ub4 counter;
	OCIParam *parmdp;
	ub2 dtype;
	text *col_name;
	ub4 col_name_len;
	ub2 fieldlen;
	ub2 stmt_type;
	sb4 parm_status;




	/* Create a XdbOracleResult */
	res = (XdbOracleResult *) malloc(sizeof(XdbOracleResult));
	bzero(res, sizeof(XdbOracleResult));
	if (!res) {
		log_error(ZONE, "Couldn't allocate result\n");
		return NULL;
	}

	res->tuples = NULL;
	res->result_stored = 0;
	res->backend = self;
	_xdbsql_result_init(&(res->base), &(self->base));

	/* create a statement */
	if (checkerr
	    (self->errhp,
	     OCIHandleAlloc((dvoid *) self->envhp,
			    (dvoid **) & res->stmthp, (ub4) OCI_HTYPE_STMT,
			    (size_t) 0, (dvoid **) 0), reporterr)) {
		free(res);
		return NULL;
	}


	/* parse query */
	if (checkerr(self->errhp, OCIStmtPrepare(res->stmthp, self->errhp,
						 (text *) query,
						 (ub4) strlen(query),
						 (ub4) OCI_NTV_SYNTAX,
						 (ub4) OCI_DEFAULT),
		     reporterr)) {
		free(res);
		return NULL;
	}

	/* We need to setup zero iters for SELECT and 1 for other queries */
	if (checkerr(self->errhp, OCIAttrGet((dvoid *) res->stmthp,
					     (ub4) OCI_HTYPE_STMT,
					     (dvoid *) & stmt_type,
					     (ub4 *) 0,
					     (ub4) OCI_ATTR_STMT_TYPE,
					     self->errhp), reporterr)) {
		OCIHandleFree((dvoid *) res->stmthp, (ub4) OCI_HTYPE_STMT);
		free(res);
		return NULL;
	}

	DebugPrint("oracle_query: %s type %d", query, stmt_type);

	/* execute */
	if (checkerr(self->errhp, OCIStmtExecute(self->svchp,
						 res->stmthp, self->errhp,
						 (ub4) (stmt_type ==
							OCI_STMT_SELECT ? 0
							: 1), (ub4) 0,
						 (OCISnapshot *) NULL,
						 (OCISnapshot *) NULL,
						 (ub4) OCI_DEFAULT |
						 OCI_COMMIT_ON_SUCCESS),
		     reporterr)) {
		OCIHandleFree((dvoid *) res->stmthp, (ub4) OCI_HTYPE_STMT);
		free(res);
		return NULL;
	}


	/* describe the statement */
	/* Request a parameter descriptor for position 1 in the select-list */
	counter = 1;
	parm_status = OCIParamGet(res->stmthp, OCI_HTYPE_STMT, self->errhp,
				  (dvoid **) & parmdp, (ub4) counter);

	/* Loop only if a descriptor was successfully retrieved for
	   current  position, starting at 1 */
	while (parm_status == OCI_SUCCESS && res->field_count < MAX_FIELDS) {
		/* Retrieve the data type attribute */
		checkerr(self->errhp,
			 OCIAttrGet((dvoid *) parmdp,
				    (ub4) OCI_DTYPE_PARAM,
				    (dvoid *) & dtype, (ub4 *) 0,
				    (ub4) OCI_ATTR_DATA_TYPE,
				    (OCIError *) self->errhp), reporterr);

		/* Retrieve the column name attribute */
		checkerr(self->errhp,
			 OCIAttrGet((dvoid *) parmdp,
				    (ub4) OCI_DTYPE_PARAM,
				    (dvoid **) & col_name,
				    (ub4 *) & col_name_len,
				    (ub4) OCI_ATTR_NAME,
				    (OCIError *) self->errhp), reporterr);

		checkerr(self->errhp,
			 OCIAttrGet((dvoid *) parmdp,
				    (ub4) OCI_DTYPE_PARAM,
				    (dvoid *) & fieldlen, (ub4 *) 0,
				    (ub4) OCI_ATTR_DATA_SIZE,
				    (OCIError *) self->errhp), 1);
		col_name[col_name_len] = 0;
		/*
		   we care about text and date fields only. Text fields will have their size stored as is, for
		   date fields we use 255, and for unsupported fields - 0
		 */
		switch (dtype) {
		case OCI_TYPECODE_VARCHAR2:
		case OCI_TYPECODE_VARCHAR:
		case OCI_TYPECODE_CHAR:
			break;
		case OCI_TYPECODE_DATE:
			fieldlen = 255;
			break;
		default:
			fieldlen = 255;
			//fieldlen = 0;
		}
		res->field_size[res->field_count] =
		    (fieldlen >
		     MAX_FIELD_SIZE ? MAX_FIELD_SIZE : fieldlen);

		/* increment counter and get next descriptor, if there is one */
		counter++;
		res->field_count++;
		parm_status = OCIParamGet(res->stmthp, OCI_HTYPE_STMT,
					  self->errhp, (dvoid **) & parmdp,
					  (ub4) counter);
	}


	return res;
}

/*
* Issue a query to the db we are connected to.
*/
XdbOracleResult *xdboracle_query(XdbOracleBackend * self,
				 const char *query)
{
	return oracle_query(self, query, 1);
}





/*
* Returns error
*
*/

const char *xdboracle_error(XdbOracleBackend * self)
{
	ub4 errcode;
	OCIErrorGet((dvoid *) self->errhp, (ub4) 1, (text *) NULL,
		    (sb4 *) & errcode, (text *) self->errbuf,
		    (ub4) sizeof(self->errbuf), (ub4) OCI_HTYPE_ERROR);
	DebugPrint("xdboracle_error: %s", self->errbuf);
	return self->errbuf;
}




short xdboracle_use_result(XdbOracleResult * res)
{
	return 1;
}




short xdboracle_store_result(XdbOracleResult * res)
{
	char *buf[MAX_FIELDS];
	OCIDefine *defhp[MAX_FIELDS];
	sb2 inds[MAX_FIELDS];
	XdbOracleBackend *self;
	int size;
	int has_more_data, i, j;
	ub4 status;
	Tuple *save_p;

	if (!res) {
		log_error(ZONE, "No result");
		return 0;
	}
	self = res->backend;
	/* If already stored, get out */
	if (res->result_stored)
		return 1;

	/* Free any old tuples */
	if (res->tuples)
		result_free_data(res);

	/* Init data */
	res->row = -1;
	res->tuple_allocated = TUPLE_INCREMENT;
	res->tuples = calloc(res->tuple_allocated, sizeof(Tuple));
	if (res->tuples == NULL) {
		log_error(ZONE, "Out of memory");
		return 0;
	}

	/* alloc individual buffers */
	for (i = 0; i < res->field_count; i++) {
		size = res->field_size[i];
		if (size == 0)
			continue;	/* Unsupported */

		/* text */
		buf[i] = malloc(size + 1);
		bzero(buf[i], size + 1);
		if (buf[i] == NULL) {
			for (; i >= 0; i--)
				free(buf[i]);
			free(res->tuples);
			res->tuples = NULL;
			log_error(ZONE, "Out of memory");
			return 0;
		}
		/* define string buffer */
		checkerr(self->errhp,
			 OCIDefineByPos(res->stmthp, &defhp[i],
					self->errhp, (ub4) i + 1,
					(dvoid *) buf[i], (sb4) size + 1,
					SQLT_STR, (dvoid *) & inds[i],
					(ub2 *) 0, (ub2 *) 0,
					(ub4) OCI_DEFAULT), 1);

	}

	/* Loop on tuples */
	has_more_data = 1;
	for (i = 0; i < MAX_TUPLES && has_more_data; i++) {
		if (i >= res->tuple_allocated) {
			/* Time to grow */
			res->tuple_allocated += TUPLE_INCREMENT;
			save_p = res->tuples;
			res->tuples = (Tuple *) realloc(res->tuples,
							res->
							tuple_allocated *
							sizeof(Tuple));
			if (res->tuples == NULL) {
				/* out of memory, won't fetch more */
				res->tuples = save_p;
				break;
			}
		}
		status = OCIStmtFetch(res->stmthp, self->errhp, (ub4) 1,
				      (ub2) OCI_FETCH_NEXT,
				      (ub4) OCI_DEFAULT);
		if (status != OCI_SUCCESS) {
			if (status != OCI_NO_DATA)
				checkerr(self->errhp, status, 1);
			has_more_data = 0;
		} else {
			tuple_alloc_fields(&res->tuples[i],
					   res->field_count);
			res->tuple_count = i + 1;
			for (j = 0; j < res->field_count; j++) {
				size = res->field_size[j];
				res->tuples[i].fields[j] =
				    malloc(size + 1);
				if (res->tuples[i].fields[j] == NULL)
					break;
				strncpy(res->tuples[i].fields[j], buf[j],
					size);
			}
		}
	}
	for (i = 0; i < res->field_count; i++)
		free(buf[i]);

	DebugPrint("xdboracle_store_result: fetched %d rows",
		   res->tuple_count);
	/* Mark result as stored and return */
	res->result_stored = 1;
	return 1;
}




/*
* Free a result.
*/
void xdboracle_free_result(XdbOracleResult * res)
{
	if (!res) {
		log_error(ZONE, "No result");
		return;
	}

	result_free_data(res);
	OCIHandleFree((dvoid *) res->stmthp, (ub4) OCI_HTYPE_STMT);
	free(res);
	DebugPrint("xdboracle_free_result: done");
}




/*
* Number of tuples in result, -1 if error
*/
int xdboracle_num_tuples(XdbOracleResult * res)
{
	if (!xdboracle_store_result(res))
		return -1;

	return res->tuple_count;
}




/*
* Number of fields in result, -1 if error
*/
int xdboracle_num_fields(XdbOracleResult * res)
{
	if (!xdboracle_store_result(res))
		return -1;

	return res->field_count;
}



/*
* Go to next tuple, 0 if error
*/
short xdboracle_next_tuple(XdbOracleResult * res)
{
	/* Read data */
	if (!xdboracle_store_result(res))
		return 0;

	/* EOF ? */
	if (res->row >= res->tuple_count - 1)
		return 0;

	++res->row;
	return 1;
}




/*
* Returns value of the field at given index in current tuple.
*/
const char *xdboracle_get_value(XdbOracleResult * res, int index)
{
	if (!xdboracle_store_result(res))
		return NULL;

	if (index < 0 || index >= res->field_count) {
		log_error(ZONE, "Index %d is not within 0 and %d", index,
			  res->field_count - 1);
		return NULL;
	}

	if (res->row >= res->tuple_count) {
		log_error(ZONE, "Already at end of result");
		return NULL;
	}

	return res->tuples[res->row].fields[index];
}




int xdboracle_escape_string(XdbOracleBackend * self, char *to,
			    const char *from, int len)
{
	unsigned char c;
	int i;

	for (i = 0; i < len; i++) {
		c = *from++;
		switch (c) {
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




/********* xdb_sql_oracle support functions *********/
/*
* Free all read data from a result
*/
static void result_free_data(XdbOracleResult * res)
{
	int pos;

	if (!res)
		return;
	if (!res->tuples)
		return;

	/* Free each tuple */
	for (pos = 0; pos < res->tuple_count; ++pos) {
		tuple_free_fields(&res->tuples[pos]);
	}

	/* Free tuples */
	free(res->tuples);
	res->tuples = NULL;
	res->result_stored = 0;
}



static void tuple_alloc_fields(Tuple * tuple, int field_count)
{
	int pos;

	tuple->field_count = field_count;
	tuple->fields = (char **) calloc(field_count, sizeof(char *));
	if (!tuple->fields) {
		log_error(ZONE,
			  "Could not allocate memory for tuple fields");
		return;
	}

	for (pos = 0; pos < field_count; ++pos) {
		tuple->fields[pos] = NULL;
	}
}






static void tuple_free_fields(Tuple * tuple)
{
	int pos;
	if (!tuple)
		return;

	if (tuple->fields) {
		for (pos = 0; pos < tuple->field_count; ++pos) {
			free(tuple->fields[pos]);
		}
	}
	free(tuple->fields);
}







static int checkerr(OCIError * errhp, sword status, int reporterr)
{
	text errbuf[512];
	ub4 errcode;
	char *errmsg;
	int res = 1;

	switch (status) {
	case OCI_SUCCESS:
		res = 0;
		break;
	case OCI_SUCCESS_WITH_INFO:
		errmsg = "Error - OCI_SUCCESS_WITH_INFO";
		res = 0;
		break;
	case OCI_NEED_DATA:
		errmsg = "Error - OCI_NEED_DATA";
		res = 0;
		break;
	case OCI_NO_DATA:
		errmsg = "Error - OCI_NO_DATA";
		res = 0;
		break;
	case OCI_ERROR:
		OCIErrorGet((dvoid *) errhp, (ub4) 1, (text *) NULL,
			    (sb4 *) & errcode, (text *) errbuf,
			    (ub4) sizeof(errbuf), (ub4) OCI_HTYPE_ERROR);
		errmsg = errbuf;
		break;
	case OCI_INVALID_HANDLE:
		errmsg = "Error - OCI_INVALID_HANDLE";
		break;
	case OCI_STILL_EXECUTING:
		res = 0;
		errmsg = "Error - OCI_STILL_EXECUTE";
		break;
	case OCI_CONTINUE:
		res = 0;
		errmsg = "Error - OCI_CONTINUE";
		break;
	default:
		res = 0;
		break;
	}
	if (res && reporterr) {
		log_error(ZONE, "xdb_sql_oracle: ERROR %s\n", errmsg);
	}
	return res;
}

#define BUFFER_SIZE 1024

static void dprintf(char *format, ...)
{
	va_list va;
	char buffer[BUFFER_SIZE];

	va_start(va, format);
	vsnprintf(buffer, BUFFER_SIZE - 2, format, va);
	strcat(buffer, "\n");
	fprintf(stderr, buffer);
	printf(buffer);
	va_end(va);

}
