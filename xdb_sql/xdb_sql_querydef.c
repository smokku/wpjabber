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

/* Changed querydef semantic a bit : qd will be instanciated when
 * parsing configuration. It will hold data found in the config of
 * a query. On each request, a req_query_def will be created, and it
 * will contain modifiable data : the query text, and the list of
 * variables to substitute. This way, the xmlnode won't have to be
 * traversed on each request, as new processing was introduced with
 * the handling of bindcol at the querydef level, for more comfort.
 */

/* a single bound variable in a query definition */
/* Changes are affecting the following functions :
 * weak_copy_var
 * bindvar_new
 */
struct query_def_bind_var_struct {
	struct query_def_bind_var_struct *next;	/* next variable binding */
	struct query_def_bind_var_struct *prev;	/* previous variable binding */
	const char *name;	/* variable name */
	const char *replace_text;	/* text to replace in query */
	const char *default_value;	/* default value to replace in query */
	int need_escape;	/* does it need escaping? */
	int is_attrib;		/* to be found in a tag or attrib */
	int length;		/* max length of a value (after escaping!) */
};

struct query_def_struct {
	pool p;			/* private memory pool for querydef */
	char *query_text;	/* the query text */
	query_def_bind_var first_var;	/* the first query variable */
	query_def_bind_var last_var;	/* the last query variable */
	XdbSqlDatas *datas;	/* inst datas */

	/* variables below are used for v2 queries - do not mix with above
	 * variables, to allow downcasting from qV2 to qV1
	 */
	query_def_bind_var user_var;	/* special variable, the user one */
	const char *namespace;	/* namespace it applies to */
	const char *type;	/* set or get ? */
	const char *purge;	/* for set, associated purge's query name */
	col_map_entry first_col;	/* for get, the first col entry */
	xmlnode resnode;	/* for get, node in which insert the result */
	xmlnode tuplenode;	/* for get, node to create for each tuple */
};

/* Handle query data for the time of a request.
 * Like a query_def, but a query_def has to be constant
 * over the server's life.
 * On the other hand, a reqquerydef is created for each request.
 * Allows for messing with query_text, and copies of variables.
 */
struct req_query_def_struct {
	pool p;			/* private memory pool for querydef */
	char *query_text;	/* the query text */
	query_def_bind_var first_var;	/* the first query variable */
	query_def_bind_var last_var;	/* the last query variable */
	XdbSqlDatas *datas;	/* xdb_sql inst data */

	/* Beware, do change member order, in order to allow downcasting
	 * from rqd to qd (poor man's OO)
	 */

	query_def qd;		/* the original data */
};



/* Single entry in the column map */
struct col_map_entry_struct {
	struct col_map_entry_struct *next;	/* pointer to next column map entry */
	const char *name;	/* name of the property */
	int index;		/* index of column in row results */

	const char *into;	/* tag to insert into */
	BColInsType instype;	/* insert as tag, or as attrib */
};

/* Column map */
struct col_map_struct {
	pool p;			/* memory pool for column map entries */
	col_map_entry first;	/* first column map entry */
	col_map_entry last;	/* last column map entry */
};

query_def xdbsql_querydef_init(XdbSqlDatas * self, xmlnode qd_root)
{
	pool p;			/* new memory pool for querydef */
	query_def qd;		/* querydef pointer */
	query_def_bind_var var;	/* new bind variable structure */
	xmlnode tmp;		/* for traversing all <querydef> children */

	/* allocate pool and querydef structure */
	p = pool_new();
	qd = (query_def) pmalloc(p, sizeof(struct query_def_struct));
	qd->p = p;
	qd->query_text = NULL;
	qd->first_var = qd->last_var = NULL;
	qd->datas = self;
	qd->first_col = NULL;	/* unused */
	qd->resnode = NULL;	/* unused */
	qd->user_var = NULL;	/* unused */

	for (tmp = xmlnode_get_firstchild(qd_root); tmp; tmp = xmlnode_get_nextsibling(tmp)) {	/* look for <text> and <bindvar> tags among the children */
		if (j_strcmp(xmlnode_get_name(tmp), "text") == 0) {
			qd->query_text =
			    (char *)
			    xmlnode_get_data(xmlnode_get_firstchild(tmp));
		} else if (j_strcmp(xmlnode_get_name(tmp), "bindvar") == 0) {	/* copy the <bindvar> tag to the list of the bind values */
			var =
			    (query_def_bind_var) pmalloc(p,
							 sizeof(struct
								query_def_bind_var_struct));
			var->name =
			    (const char *) xmlnode_get_attrib(tmp, "name");
			var->replace_text =
			    (const char *)
			    xmlnode_get_data(xmlnode_get_firstchild(tmp));
			var->default_value =
			    (const char *) xmlnode_get_attrib(tmp,
							      "default");
			var->need_escape =
			    (is_false(xmlnode_get_attrib(tmp, "escape")) ?
			     0 : 1);
			var->length =
			    (xmlnode_get_attrib(tmp, "length") !=
			     NULL ? atoi(xmlnode_get_attrib(tmp, "length"))
			     : 0);
			var->next = NULL;
			var->prev = qd->last_var;
			if (qd->last_var)
				qd->last_var->next = var;
			else
				qd->first_var = var;
			qd->last_var = var;

		}
		/* end else if */
	}			/* end for */

	return qd;

}				/* end xdbsql_querydef_init */

static void do_replace(query_def qd,	/* query def buffer containing string to be morphed */
		       const query_def_bind_var var,	/* variable definition to replace */
		       const char *value)
{				/* value to replace variable with - NULL to delete */
	const char *repl_value = NULL;	/* replacement text (properly escaped) */
	spool s;		/* builds the new query string */
	int var_len, head_len;	/* length of the replacement variable and string "head" */
	char *esc_buffer;	/* used to SQL-escape the replacement text */
	char *buffer;		/* used to make the "head" into a proper string */
	char *point, *next;	/* used in searching the original query string */
	const char *ef_value;	/* value that we will use for replacement */

	/* first, make sure that we actually DO have a variable to replace! */
	next = strstr(qd->query_text, var->replace_text);
	if (!next)
		return;

	ef_value = value;
	if (ef_value && strlen(ef_value) == 0 && var->default_value)
		ef_value = var->default_value;
	if (ef_value) {		/* may need to escape the value */
		int repl_len = strlen(ef_value);
		if (var->need_escape) {	/* need to SQL-encode the replacement text! */
			esc_buffer =
			    (char *) alloca((repl_len * 2 + 1) *
					    sizeof(char));
			sqldb_escape_string(qd->datas, esc_buffer,
					    ef_value, repl_len);
		} else {	/* the string can go as is */

			esc_buffer = (char *) alloca(repl_len + 1);
			strcpy(esc_buffer, ef_value);
		}
		if (var->length > 0 && strlen(esc_buffer) > var->length)
			esc_buffer[var->length] = '\0';	/* truncate the value */
		repl_value = (const char *) esc_buffer;
	}

	/* initialize other variables needed during the replacement loop */
	point = qd->query_text;
	var_len = strlen(var->replace_text);
	/* buffer is allocated as the maximum possible size of any "head" string */
	buffer =
	    (char *) alloca((strlen(qd->query_text) - var_len + 1) *
			    sizeof(char));
	s = spool_new(qd->p);

	while (next) {		/* figure out how much we have to copy before the replacement text */
		head_len = (int) (next - point);
		if (head_len > 0) {	/* copy the "head" of the buffer and add it */
			memcpy(buffer, point, head_len * sizeof(char));
			buffer[head_len] = '\0';
			spool_add(s, buffer);

		}

		/* end if */
		/* add the replacement value */
		spool_add(s, (char *) repl_value);

		/* look for the next instance */
		point = next + var_len;
		next = strstr(point, var->replace_text);

	}			/* end while */

	/* add the rest of the "tail" and save the resulting string */
	spool_add(s, point);
	qd->query_text = spool_print(s);
}				/* end do_replace */

void xdbsql_querydef_setvar(query_def qd, const char *name,
			    const char *value)
{
	query_def_bind_var var;	/* the variable we're replacing */

	/* look for a matching variable definition */
	for (var = qd->first_var; var; var = var->next)
		if (j_strcmp(name, var->name) == 0)
			break;

	if (!var)
		return;		/* no suitable variable definition found */

	/* take this variable definition out of the list */
	if (var->next)
		var->next->prev = var->prev;
	else
		qd->last_var = var->prev;
	if (var->prev)
		var->prev->next = var->next;
	else
		qd->first_var = var->next;

	/* Perform the variable replacement... */
	do_replace(qd, var, value);

}				/* end xdbsql_querydef_setvar */

char *xdbsql_querydef_finalize(query_def qd)
{
	query_def_bind_var var;	/* used to default out all unspecified values */

	while (qd->first_var) {	/* take the first variable off the list */
		var = qd->first_var;
		qd->first_var = var->next;
		if (qd->first_var)
			qd->first_var->prev = NULL;

		/* replace its default value into the text */
		do_replace(qd, var, var->default_value);

	}			/* end while */

	return qd->query_text;

}				/* end xdbsql_querydef_finalize */

void xdbsql_querydef_free(query_def qd)
{
	pool_free(qd->p);

}				/* end xdbsql_querydef_free */

col_map xdbsql_colmap_init(xmlnode qd_root)
{
	pool p;			/* new memory pool for colmap */
	col_map cm;		/* new colmap to return */
	xmlnode tmp;		/* for traversing all <querydef> children */
	col_map_entry ent;	/* new colmap entry */

	p = pool_new();
	cm = (col_map) pmalloc(p, sizeof(struct col_map_struct));
	cm->p = p;
	cm->first = cm->last = NULL;

	for (tmp = xmlnode_get_firstchild(qd_root); tmp; tmp = xmlnode_get_nextsibling(tmp)) {	/* look for all <bindcol> tags under the <querydef> */
		if (j_strcmp(xmlnode_get_name(tmp), "bindcol") == 0) {	/* allocate a column map entry for this column */
			ent =
			    (col_map_entry) pmalloc(p,
						    sizeof(struct
							   col_map_entry_struct));
			ent->next = NULL;
			ent->name =
			    (const char *) xmlnode_get_attrib(tmp, "name");
			ent->index =
			    atoi(xmlnode_get_attrib(tmp, "offset"));
			if (cm->last)
				cm->last->next = ent;
			else
				cm->first = ent;
			cm->last = ent;

		}
		/* end if */
	}			/* end for */

	return cm;

}				/* end xdbsql_colmap_init */

void xdbsql_colmap_free(col_map cm)
{
	pool_free(cm->p);

}				/* end xdbsql_colmap_free */

int xdbsql_colmap_index(col_map cm, const char *name)
{
	col_map_entry ent;
	for (ent = cm->first; ent; ent = ent->next)
		if (j_strcmp(ent->name, name) == 0)
			return ent->index;

	return -1;

}				/* end xdbsql_colmap_index */

static query_def_bind_var
bindvar_new(pool p, const char *name, int is_attrib,
	    const char *replace_text, const char *default_value,
	    int need_escape)
{
	query_def_bind_var var;

	var =
	    (query_def_bind_var) pmalloc(p,
					 sizeof(struct
						query_def_bind_var_struct));
	var->name = name;
	var->is_attrib = is_attrib;
	var->replace_text = replace_text;
	var->default_value = default_value;
	var->need_escape = need_escape;
	var->next = NULL;
	var->prev = NULL;
	return var;
}

/* Get the node given as child of the given node
 */
static int process_node_param(xmlnode x, xmlnode * ptr)
{
	xmlnode res;

	if (!ptr)
		return 0;

	if (*ptr) {
		log_error(NULL,
			  "[xdbsql_simple_querydef_init]"
			  " at most 1 '%s' element is allowed",
			  xmlnode_get_name(x));
		return 0;
	}
	for (res = xmlnode_get_firstchild(x);
	     res != NULL; res = xmlnode_get_nextsibling(res)) {
		if ((xmlnode_get_type(res) != NTYPE_TAG)
		    || (xmlnode_get_name(res) == NULL))
			continue;
		else
			break;
	}
	*ptr = res;
	if (*ptr == NULL) {
		log_error(NULL, "[xdbsql_simple_querydef_init]"
			  " missing child tag for '%s'",
			  xmlnode_get_name(x));
		return 0;
	}
	return 1;
}

static col_map_entry process_col(xmlnode x, pool p)
{
	col_map_entry col;
	const char *offset;

	if (!x || !p)
		return NULL;

	col =
	    (col_map_entry) pmalloc(p,
				    sizeof(struct col_map_entry_struct));
	/*
	 * Get name and type of insertion
	 */
	col->name = xmlnode_get_attrib(x, "tag");
	if (col->name) {
		col->instype = INS_TAG;
		if (xmlnode_get_attrib(x, "attrib")) {
			log_error(NULL,
				  "[xdbsql_simple_querydef_init] cannot handle bindcol :"
				  " needs only one of 'tag' or 'attrib' attribute");
		}
	} else {
		col->name = xmlnode_get_attrib(x, "attrib");
		if (col->name) {
			col->instype = INS_ATTRIB;
		} else {
			log_error(NULL,
				  "[xdbsql_simple_querydef_init] cannot handle bindcol :"
				  " needs 'tag' or 'attrib' specification");
			return NULL;
		}
	}

	/*
	 * Handle offset.
	 */
	col->index = -1;
	offset = xmlnode_get_attrib(x, "offset");
	if (offset && *offset)
		col->index = atoi(offset);
	if (col->index == -1) {
		log_error(NULL,
			  "[xdbsql_simple_querydef_init] cannot handle bindcol :"
			  " needs offset specification");
		return NULL;
	}

	/* Where should we insert this value ?
	 * If not specified, it will be in item, then in rc.
	 */
	col->into = xmlnode_get_attrib(x, "insert-into");
	return col;
}

/*
 * Called once per query at configuration time.
 * Does validation too. Return NULL on error.
 */
/* simple validator:
 * <text> is given
 * <user> is given
 * (get only) <top-result> has a child tag
 * (get only) at least one <bindcol>
 * (get only) <bindcol> has (tag or attrib), and offset
 */
query_def xdbsql_querydef_init_v2(XdbSqlDatas * self, pool p, xmlnode qd_root)
{
	query_def qd;		/* querydef pointer */
	query_def_bind_var var;	/* new bind variable structure */
	xmlnode tmp;		/* for traversing all <querydef> children */

	/* allocate pool and querydef structure */
	qd = (query_def) pmalloc(p, sizeof(struct query_def_struct));
	qd->p = p;
	qd->query_text = NULL;
	qd->resnode = qd->tuplenode = NULL;
	qd->first_var = qd->last_var = qd->user_var = NULL;
	qd->first_col = NULL;
	qd->datas = self;

	qd->namespace = xmlnode_get_attrib(qd_root, "namespace");
	if (!qd->namespace || !*qd->namespace) {
		log_error(NULL,
			  "[xdbsql_simple_querydef_init] 'namespace' attrib missing in query");
		return NULL;
	}

	qd->type = xmlnode_get_attrib(qd_root, "type");
	if (!qd->type || !*qd->type) {
		log_error(NULL,
			  "[xdbsql_simple_querydef_init] 'type' attrib missing in query");
		return NULL;
	}

	qd->purge = xmlnode_get_tag_data(qd_root, "purge");

	for (tmp = xmlnode_get_firstchild(qd_root); tmp;
	     tmp = xmlnode_get_nextsibling(tmp)) {
		if (j_strcmp(xmlnode_get_name(tmp), "text") == 0) {
			if (qd->query_text) {
				log_error(NULL,
					  "[xdbsql_simple_querydef_init] text defined more than once");
				return NULL;
			}
			qd->query_text =
			    (char *)
			    xmlnode_get_data(xmlnode_get_firstchild(tmp));
		} else if (j_strcmp(xmlnode_get_name(tmp), "user") == 0) {
			if (qd->user_var) {
				log_error(NULL,
					  "[xdbsql_simple_querydef_init] user defined more than once");
				return NULL;
			}

			qd->user_var = bindvar_new(p, NULL, 0,
						   (const char *)
						   xmlnode_get_data
						   (xmlnode_get_firstchild
						    (tmp)), NULL, 0);
		} else if (j_strcmp(xmlnode_get_name(tmp), "bindvar") == 0) {	/* copy the <bindvar> tag to the list of the bind values */
			const char *name;
			int is_attr = 0;

			name =
			    (const char *) xmlnode_get_attrib(tmp, "tag");
			if (!name) {
				name =
				    (const char *) xmlnode_get_attrib(tmp,
								      "attrib");
				is_attr = 1;
			}
			if (!name) {
				log_error(NULL,
					  "[xdbsql_simple_querydef_init]"
					  " missing 'tag' or 'attrib' for bindvar");
				return NULL;
			}

			var = bindvar_new(p,
					  name, is_attr,
					  (const char *)
					  xmlnode_get_data
					  (xmlnode_get_firstchild(tmp)),
					  (const char *)
					  xmlnode_get_attrib(tmp,
							     "default"),
					  (is_false
					   (xmlnode_get_attrib
					    (tmp, "escape")) ? 0 : 1));

			var->prev = qd->last_var;
			if (qd->last_var)
				qd->last_var->next = var;
			else
				qd->first_var = var;
			qd->last_var = var;
		} else if (j_strcmp(xmlnode_get_name(tmp), "top-result") ==
			   0) {
			/* (top-result) */
			if (!process_node_param(tmp, &qd->resnode))
				return NULL;
		} else if (j_strcmp(xmlnode_get_name(tmp), "tuple-node") ==
			   0) {
			/* (tuple-node)? */

			if (!process_node_param(tmp, &qd->tuplenode))
				return NULL;
		} else if (j_strcmp(xmlnode_get_name(tmp), "bindcol") == 0) {
			col_map_entry col;

			col = process_col(tmp, p);
			if (!col)
				return NULL;
			col->next = qd->first_col;
			qd->first_col = col;
		}
	}			/* end for */

	if (!qd->query_text) {
		log_error(NULL,
			  "[xdbsql_simple_querydef_init] needs query text");
		return NULL;
	}
	if (!qd->user_var) {
		log_error(NULL,
			  "[xdbsql_simple_querydef_init] needs user");
		return NULL;
	}
/*	if (!qd->resnode) */
/*	{ */
/*	log_error(NULL, */
/*		  "[xdbsql_simple_querydef_init] needs result node"); */
/*	return NULL; */
/*	} */
/*	if (!qd->first_col) */
/*	{ */
/*	log_error(NULL, */
/*		  "[xdbsql_simple_querydef_init] needs at least one col"); */
/*	return NULL; */
/*	} */

	return qd;

}				/* end xdbsql_simple_querydef_init */


/* create a new var, copy pointers to strings */
static query_def_bind_var
weak_copy_var(pool p, const query_def_bind_var v1)
{
	query_def_bind_var var;

	var =
	    (query_def_bind_var) pmalloc(p,
					 sizeof(struct
						query_def_bind_var_struct));
	var->name = v1->name;
	var->replace_text = v1->replace_text;
	var->default_value = v1->default_value;
	var->need_escape = v1->need_escape;
	var->is_attrib = v1->is_attrib;
	var->next = var->prev = NULL;
	return var;
}

/*
 * Init a rqd from a qd. A qd lives as long as the xdb_sql instance,
 * and is never modified after creation, while a rqd lives for the time
 * of a request.
 */
req_query_def xdbsql_reqquerydef_init(const query_def qd, const char *user)
{
	pool p;			/* new memory pool for reqquerydef */
	req_query_def reqqd;	/* reqquerydef pointer */
	query_def_bind_var var;	/* new bind variable structure */
	query_def_bind_var v;	/* variable structure */

	/* allocate pool and querydef structure */
	p = pool_new();
	reqqd =
	    (req_query_def) pmalloc(p,
				    sizeof(struct req_query_def_struct));
	reqqd->p = p;
	reqqd->query_text = NULL;
	reqqd->first_var = reqqd->last_var = NULL;
	reqqd->datas = qd->datas;
	reqqd->qd = qd;

	reqqd->query_text = pstrdup(reqqd->p, qd->query_text);

	for (v = qd->first_var; v != NULL; v = v->next) {
		var = weak_copy_var(reqqd->p, v);
		v->prev = reqqd->last_var;
		if (reqqd->last_var)
			reqqd->last_var->next = var;
		else
			reqqd->first_var = var;
		reqqd->last_var = var;
	}

	if (qd->user_var == NULL) {
		log_error(NULL,
			  "xdbsql_reqquerydef_init : qd->user_var == NULL");
		xdbsql_reqquerydef_free(reqqd);
		return NULL;
	}

	do_replace((query_def) reqqd, qd->user_var, user);

	return reqqd;
}				/* end xdbsql_reqquerydef_init */

char *xdbsql_reqquerydef_finalize(req_query_def rqd)
{
	return xdbsql_querydef_finalize((query_def) rqd);
}				/* end xdbsql_reqquerydef_finalize */


void xdbsql_reqquerydef_free(req_query_def rqd)
{
	pool_free(rqd->p);
}				/* end xdbsql_reqquerydef_free */


xmlnode xdbsql_querydef_get_resnode(query_def qd)
{
	if (!qd)
		return NULL;
	return qd->resnode;
}


xmlnode xdbsql_querydef_get_tuplenode(query_def qd)
{
	if (!qd)
		return NULL;
	return qd->tuplenode;
}

const char *xdbsql_querydef_get_namespace(query_def qd)
{
	if (!qd)
		return NULL;
	return qd->namespace;
}

const char *xdbsql_querydef_get_type(query_def qd)
{
	if (!qd)
		return NULL;
	return qd->type;
}

const char *xdbsql_querydef_get_purge(query_def qd)
{
	if (!qd)
		return NULL;
	return qd->purge;
}

col_map_entry xdbsql_querydef_get_firstcol(query_def qd)
{
	if (!qd)
		return NULL;
	return qd->first_col;
}

col_map_entry xdbsql_querydef_get_nextcol(col_map_entry col)
{
	if (!col)
		return NULL;
	return col->next;
}

int xdbsql_querydef_col_get_offset(col_map_entry col)
{
	if (!col)
		return -1;
	return col->index;
}

const char *xdbsql_querydef_col_get_into(col_map_entry col)
{
	if (!col)
		return NULL;
	return col->into;
}

const char *xdbsql_querydef_col_get_name(col_map_entry col)
{
	if (!col)
		return NULL;
	return col->name;
}

BColInsType xdbsql_querydef_col_get_instype(col_map_entry col)
{
	if (!col)
		return INS_TAG;
	return col->instype;
}

query_def_bind_var xdbsql_reqquerydef_get_firstvar(req_query_def rqd)
{
	if (!rqd)
		return NULL;
	return rqd->first_var;
}

query_def_bind_var xdbsql_reqquerydef_get_nextvar(query_def_bind_var var)
{
	if (!var)
		return NULL;
	return var->next;
}

const char *xdbsql_var_get_name(query_def_bind_var var)
{
	if (!var)
		return NULL;
	return var->name;
}

int xdbsql_var_is_attrib(query_def_bind_var var)
{
	if (!var)
		return 0;
	return var->is_attrib;
}
