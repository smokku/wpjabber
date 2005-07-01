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

#define GET_CHILD_DATA(X) xmlnode_get_data(xmlnode_get_firstchild(X))

xmlnode xdbsql_vcard_get(XdbSqlDatas *self, const char *user){
    xmlnode rc = NULL;	       /* return from this function */
    xmlnode x  = NULL;	       /* */
    xmlnode query;	       /* the query for this function */
    query_def qd;	       /* the query definition */
    XdbSqlResult *result;      /* pointer to database result */
    int first = 1;	       /* first time through loop? */
    col_map map;	       /* column map variable */
    const char *sptr;	       /* string pointer */
    int ndx_full_name ;
    int ndx_first_name;
    int ndx_last_name ;
    int ndx_nick_name ;
    int ndx_url       ;
    int ndx_address1  ;
    int ndx_address2  ;
    int ndx_locality  ;
    int ndx_region    ;
    int ndx_pcode     ;
    int ndx_country   ;
    int ndx_telephone ;
    int ndx_email     ;
    int ndx_orgname   ;
    int ndx_orgunit   ;
    int ndx_title     ;
    int ndx_role      ;
    int ndx_b_day     ;
    int ndx_descr     ;
    int ndx_photo_t   ;
    int ndx_photo_b   ;


    if (!user){ /* the user was not specified - we have to bug off */
	log_error(NULL,"[xdbsql_vcard_get] user not specified");
	return NULL;

    } /* end if */

    /* Get the query definition. */
    query = xdbsql_query_get(self, "vcard-get");

    if (!query){ /* no query - eep! */
	log_error(NULL,"--!!-- WTF? vcard-get query not found?");
	return NULL;

    } /* end if */

    /* Build the query string. */
    qd = xdbsql_querydef_init(self, query);
    xdbsql_querydef_setvar(qd,"user",user);

    /* Execute the query! */
    result = sqldb_query(self,xdbsql_querydef_finalize(qd));
    xdbsql_querydef_free(qd);
    if (!result){ /* the query failed - bail out! */
	log_error(NULL,"[xdbsql_vcard_get] query failed : %s",
		  sqldb_error(self));
	return NULL;

    } /* end if */

    /* get the results from the query */
    if (!sqldb_use_result(result)){ /* unable to retrieve the result */
	log_error(NULL,"[xdbsql_vcard_get] result fetch failed : %s",
		  sqldb_error(self));
	return NULL;

    } /* end if */

    /* Initialize the return value. */
    rc = xmlnode_new_tag("vCard");
    xmlnode_put_attrib(rc,"xmlns",NS_VCARD);

    while (sqldb_next_tuple(result)){ /* look for all vcard vCards and add them to the list */
	if (first){ /* figure out all the indexes for our columns */

	    map = xdbsql_colmap_init(query);
	    ndx_full_name  = xdbsql_colmap_index(map,"full_name" );
	    ndx_first_name = xdbsql_colmap_index(map,"first_name");
	    ndx_last_name  = xdbsql_colmap_index(map,"last_name" );
	    ndx_nick_name  = xdbsql_colmap_index(map,"nick_name" );
	    ndx_url	   = xdbsql_colmap_index(map,"url"	 );
	    ndx_address1   = xdbsql_colmap_index(map,"address1"  );
	    ndx_address2   = xdbsql_colmap_index(map,"address2"  );
	    ndx_locality   = xdbsql_colmap_index(map,"locality"  );
	    ndx_region	   = xdbsql_colmap_index(map,"region"	 );
	    ndx_pcode	   = xdbsql_colmap_index(map,"pcode"	 );
	    ndx_country    = xdbsql_colmap_index(map,"country"	 );
	    ndx_telephone  = xdbsql_colmap_index(map,"telephone" );
	    ndx_email	   = xdbsql_colmap_index(map,"email"	 );
	    ndx_orgname    = xdbsql_colmap_index(map,"orgname"	 );
	    ndx_orgunit    = xdbsql_colmap_index(map,"orgunit"	 );
	    ndx_title	   = xdbsql_colmap_index(map,"title"	 );
	    ndx_role	   = xdbsql_colmap_index(map,"role"	 );
	    ndx_b_day	   = xdbsql_colmap_index(map,"b_day"	 );
	    ndx_descr	   = xdbsql_colmap_index(map,"descr"	 );
	    ndx_photo_t	   = xdbsql_colmap_index(map,"photo_type");
	    ndx_photo_b	   = xdbsql_colmap_index(map,"photo_bin" );

	    xdbsql_colmap_free(map);
	    first = 0;

	} /* end if */

	sptr = sqldb_get_value(result, ndx_full_name );
	if (sptr && *sptr)
	  xmlnode_insert_cdata(xmlnode_insert_tag(rc,"FN" ),sptr,-1);

	/* begin name node */
	x = xmlnode_insert_tag(rc,"N");
	sptr = sqldb_get_value(result, ndx_first_name);
	if (sptr && *sptr)
	  xmlnode_insert_cdata(xmlnode_insert_tag(x,"GIVEN"),sptr,-1);

	sptr = sqldb_get_value(result, ndx_last_name );
	if (sptr && *sptr)
	  xmlnode_insert_cdata(xmlnode_insert_tag(x,"FAMILY"),sptr,-1);
	/* end name node */

	sptr = sqldb_get_value(result, ndx_nick_name );
	if (sptr && *sptr)
	  xmlnode_insert_cdata(xmlnode_insert_tag(rc,"NICKNAME"),sptr,-1);

	sptr = sqldb_get_value(result, ndx_url);
	if (sptr && *sptr)
	  xmlnode_insert_cdata(xmlnode_insert_tag(rc,"URL"),sptr,-1);

	/* begin address node */
	x = xmlnode_insert_tag(rc,"ADR");
	sptr = sqldb_get_value(result, ndx_address1  );
	if (sptr && *sptr)
	  xmlnode_insert_cdata(xmlnode_insert_tag(x,"STREET"),sptr,-1);

	sptr = sqldb_get_value(result, ndx_address2  );
	if (sptr && *sptr)
	  xmlnode_insert_cdata(xmlnode_insert_tag(x,"EXTADD"),sptr,-1);

	sptr = sqldb_get_value(result, ndx_locality  );
	if (sptr && *sptr)
	  xmlnode_insert_cdata(xmlnode_insert_tag(x,"LOCALITY"	),sptr,-1);

	sptr = sqldb_get_value(result, ndx_region    );
	if (sptr && *sptr)
	  xmlnode_insert_cdata(xmlnode_insert_tag(x,"REGION"	),sptr,-1);

	sptr = sqldb_get_value(result, ndx_pcode     );
	if (sptr && *sptr)
	  xmlnode_insert_cdata(xmlnode_insert_tag(x,"PCODE"	),sptr,-1);

	sptr = sqldb_get_value(result, ndx_country   );
	if (sptr && *sptr)
	  xmlnode_insert_cdata(xmlnode_insert_tag(x,"CTRY"	),sptr,-1);
	/* end address node */

	sptr = sqldb_get_value(result, ndx_telephone );
	if (sptr && *sptr)
	  xmlnode_insert_cdata(xmlnode_insert_tag(xmlnode_insert_tag(rc,"TEL"),"NUMBER"),sptr,-1);

	sptr = sqldb_get_value(result, ndx_email     );
	if (sptr && *sptr)
	  xmlnode_insert_cdata(xmlnode_insert_tag(xmlnode_insert_tag(rc,"EMAIL"),"USERID"),sptr,-1);

	/* begin org node */
	x = xmlnode_insert_tag(rc,"ORG");
	sptr = sqldb_get_value(result, ndx_orgname   );
	if (sptr && *sptr)
	  xmlnode_insert_cdata(xmlnode_insert_tag(x,"ORGNAME"	),sptr,-1);

	sptr = sqldb_get_value(result, ndx_orgunit   );
	if (sptr && *sptr)
	  xmlnode_insert_cdata(xmlnode_insert_tag(x,"ORGUNIT"	),sptr,-1);
	/* end org node */

	sptr = sqldb_get_value(result, ndx_title     );
	if (sptr && *sptr)
	  xmlnode_insert_cdata(xmlnode_insert_tag(rc,"TITLE"	 ),sptr,-1);

	sptr = sqldb_get_value(result, ndx_role      );
	if (sptr && *sptr)
	  xmlnode_insert_cdata(xmlnode_insert_tag(rc,"ROLE"	 ),sptr,-1);

	sptr = sqldb_get_value(result, ndx_b_day     );
	if (sptr && *sptr)
	  xmlnode_insert_cdata(xmlnode_insert_tag(rc,"BDAY"	),sptr,-1);

	sptr = sqldb_get_value(result, ndx_descr     );
	if (sptr && *sptr)
	  xmlnode_insert_cdata(xmlnode_insert_tag(rc,"DESC"	),sptr,-1);

	/* begin photo node */
	sptr = sqldb_get_value(result, ndx_photo_t);
	if (sptr && *sptr) {
	  x = xmlnode_insert_tag(rc,"PHOTO");
	  xmlnode_insert_cdata(xmlnode_insert_tag(x,"TYPE"),sptr,-1);
	  sptr = sqldb_get_value(result, ndx_photo_b);
	  if (sptr && *sptr)
	    xmlnode_insert_cdata(xmlnode_insert_tag(rc,"BINVAL"),sptr,-1);
	}
	/* end photo node */

    } /* end while */

    sqldb_free_result(result);

    return rc;

} /* end xdbsql_vcard_get */

static int purge_vcard(XdbSqlDatas *self,
			const char *user){
    xmlnode query;	       /* the query for this function */
    query_def qd;	       /* the query definition */
    XdbSqlResult *result;      /* return code from query */

    /* Get the query definition. */
    query = xdbsql_query_get(self, "vcard-remove");

    if (!query){ /* no query - eep! */
	log_error(NULL,"--!!-- WTF? vcard-remove query not found?");
	return 0;

    } /* end if */

    /* Build the query string. */
    qd = xdbsql_querydef_init(self, query);
    xdbsql_querydef_setvar(qd,"user",user);

    /* Execute the query! */
    result = sqldb_query(self, xdbsql_querydef_finalize(qd));
    xdbsql_querydef_free(qd);
    if (!result){ /* the database query failed */
	log_error(NULL,"[purge_vcard] query failed");
	return 0;

    } /* end if */
    sqldb_free_result(result);
    return 1;  /* Qa'pla! */

} /* end purge_vcard */

static int test_root_vcard_node(
    xmlnode root){

    return 1;  /* ship it */

} /* end test_root_vcard_node */

void validate_iso8601_date(char *sdate, char *vdate, int size){
    struct tm tm;
    memset(&tm, 0, sizeof(struct tm));
    strptime(sdate, "%Y-%m-%dT%H:%M:%S", &tm);
    strftime(vdate, size,  "%Y-%m-%dT%H:%M:%S", &tm);
}

int xdbsql_vcard_set(XdbSqlDatas *self, const char *user, xmlnode data){
    xmlnode query;	       /* the query for this function */
    xmlnode x, x2;	       /* used to iterate through the rules */
    query_def qd;	       /* the query definition */
    XdbSqlResult *result;      /* return from query */
    char *data_full_name  = NULL;
    char *data_first_name = NULL;
    char *data_last_name  = NULL;
    char *data_nick_name  = NULL;
    char *data_url	  = NULL;
    char *data_address1   = NULL;
    char *data_address2   = NULL;
    char *data_locality   = NULL;
    char *data_region	  = NULL;
    char *data_pcode	  = NULL;
    char *data_country	  = NULL;
    char *data_telephone  = NULL;
    char *data_email	  = NULL;
    char *data_orgname	  = NULL;
    char *data_orgunit	  = NULL;
    char *data_title	  = NULL;
    char *data_role	  = NULL;
    char *data_b_day	  = NULL;
    char *data_descr	  = NULL;
    char *data_photo_t	  = NULL;
    char *data_photo_b	  = NULL;
    const char *querystring;
    char *name;
    char b_day_8601[32];
    int items = 0;	/* count of parsed vcard items */

    if (!user){ /* the user was not specified - we have to bug off */
	log_error(NULL,"[xdbsql_vcard_set] user not specified");
	return 0;

    } /* end if */

    if (!data)
	return purge_vcard(self, user);

    if (!test_root_vcard_node(data))
	return 0;  /* error already reported */

    /* Get the query definition. */
    query = xdbsql_query_get(self, "vcard-set");

    if (!query){ /* no query - eep! */
	log_error(NULL,"--!!-- WTF? vcard-set query not found?");
	return 0;

    } /* end if */

    /* Purge any existing vcard messages. */
    (void)purge_vcard(self, user);

    /* Start to build the query string. */
    qd = xdbsql_querydef_init(self, query);
    xdbsql_querydef_setvar(qd,"user",user);

    for (x=xmlnode_get_firstchild(data); x; x=xmlnode_get_nextsibling(x)){
      items++;
      name = xmlnode_get_name(x);
      if (j_strcmp(name,"FN")==0)
	data_full_name	= GET_CHILD_DATA(x);
      else if (j_strcmp(name,"N")==0){
	for (x2=xmlnode_get_firstchild(x);x2;x2=xmlnode_get_nextsibling(x2)){
	  name = xmlnode_get_name(x2);
	  if (j_strcmp(name,"GIVEN")==0)
	    data_first_name = GET_CHILD_DATA(x2);
	  else if (j_strcmp(name,"FAMILY")==0)
	    data_last_name  = GET_CHILD_DATA(x2);
	}
      }
      else if (j_strcmp(name,"NICKNAME")==0)
	data_nick_name	= GET_CHILD_DATA(x);
      else if (j_strcmp(name,"URL")==0)
	data_url	= GET_CHILD_DATA(x);
      else if (j_strcmp(name,"ADR")==0){
	for (x2=xmlnode_get_firstchild(x);x2;x2=xmlnode_get_nextsibling(x2)){
	  name = xmlnode_get_name(x2);
	  if (j_strcmp(name,"STREET")==0)
	    data_address1   = GET_CHILD_DATA(x2);
	  else if (j_strcmp(name,"EXTADD")==0)
	    data_address2   = GET_CHILD_DATA(x2);
	  else if (j_strcmp(name,"LOCALITY")==0)
	    data_locality   = GET_CHILD_DATA(x2);
	  else if (j_strcmp(name,"REGION")==0)
	    data_region     = GET_CHILD_DATA(x2);
	  else if (j_strcmp(name,"PCODE")==0)
	    data_pcode	    = GET_CHILD_DATA(x2);
	  else if (j_strcmp(name,"CTRY")==0)
	    data_country    = GET_CHILD_DATA(x2);
	}
      }
      else if (j_strcmp(name,"TEL")==0){
	for (x2=xmlnode_get_firstchild(x);x2;x2=xmlnode_get_nextsibling(x2)){
	  name = xmlnode_get_name(x2);
	  if (j_strcmp(name,"NUMBER")==0)
	    data_telephone  = GET_CHILD_DATA(x2);
	}
      }
      else if (j_strcmp(name,"EMAIL")==0){
	for (x2=xmlnode_get_firstchild(x);x2;x2=xmlnode_get_nextsibling(x2)){
	  name = xmlnode_get_name(x2);
	  if (j_strcmp(name,"USERID")==0)
	    data_email      = GET_CHILD_DATA(x2);
	}
      }
      else if (j_strcmp(name,"ORG")==0){
	for (x2=xmlnode_get_firstchild(x);x2;x2=xmlnode_get_nextsibling(x2)){
	  name = xmlnode_get_name(x2);
	  if (j_strcmp(name,"ORGNAME")==0)
	    data_orgname    = GET_CHILD_DATA(x2);
	  else if (j_strcmp(name,"ORGUNIT")==0)
	    data_orgunit    = GET_CHILD_DATA(x2);
	}
      }
      else if (j_strcmp(name,"TITLE")==0)
	data_title	= GET_CHILD_DATA(x);
      else if (j_strcmp(name,"ROLE")==0)
	data_role	= GET_CHILD_DATA(x);
      else if (j_strcmp(name,"BDAY")==0)
	data_b_day	= GET_CHILD_DATA(x);
      else if (j_strcmp(name,"DESC")==0)
	data_descr	= GET_CHILD_DATA(x);
      else if (j_strcmp(name,"PHOTO")==0){
	for (x2=xmlnode_get_firstchild(x);x2;x2=xmlnode_get_nextsibling(x2)){
	  name = xmlnode_get_name(x2);
	  if (j_strcmp(name,"TYPE")==0)
	    data_photo_t    = GET_CHILD_DATA(x2);
	  else if (j_strcmp(name,"BINVAL")==0)
	    data_photo_b    = GET_CHILD_DATA(x2);
	}
      }
    } /* end for */

    if (0 == items)
	return 1;

    if (!data_b_day)
	data_b_day = "";

    validate_iso8601_date(data_b_day, b_day_8601, 28);
    log_debug("data_b_day: (%s), b_day_8601: (%s)", data_b_day, b_day_8601);

    /* Fill in the rest of the query parameters. */
    if (data_full_name	&& *data_full_name )
      xdbsql_querydef_setvar(qd,"full_name", data_full_name );
    if (data_first_name && *data_first_name)
      xdbsql_querydef_setvar(qd,"first_name",data_first_name);
    if (data_last_name	&& *data_last_name )
      xdbsql_querydef_setvar(qd,"last_name", data_last_name );
    if (data_nick_name	&& *data_nick_name )
      xdbsql_querydef_setvar(qd,"nick_name", data_nick_name );
    if (data_url	&& *data_url	   )
      xdbsql_querydef_setvar(qd,"url",	     data_url	    );
    if (data_address1	&& *data_address1  )
      xdbsql_querydef_setvar(qd,"address1",  data_address1  );
    if (data_address2	&& *data_address2  )
      xdbsql_querydef_setvar(qd,"address2",  data_address2  );
    if (data_locality	&& *data_locality  )
      xdbsql_querydef_setvar(qd,"locality",  data_locality  );
    if (data_region	&& *data_region    )
      xdbsql_querydef_setvar(qd,"region",    data_region    );
    if (data_pcode	&& *data_pcode	   )
      xdbsql_querydef_setvar(qd,"pcode",     data_pcode     );
    if (data_country	&& *data_country   )
      xdbsql_querydef_setvar(qd,"country",   data_country   );
    if (data_telephone	&& *data_telephone )
      xdbsql_querydef_setvar(qd,"telephone", data_telephone );
    if (data_email	&& *data_email	   )
      xdbsql_querydef_setvar(qd,"email",     data_email     );
    if (data_orgname	&& *data_orgname   )
      xdbsql_querydef_setvar(qd,"orgname",   data_orgname   );
    if (data_orgunit	&& *data_orgunit   )
      xdbsql_querydef_setvar(qd,"orgunit",   data_orgunit   );
    if (data_title	&& *data_title	   )
      xdbsql_querydef_setvar(qd,"title",     data_title     );
    if (data_role	&& *data_role	   )
      xdbsql_querydef_setvar(qd,"role",      data_role	    );
    xdbsql_querydef_setvar(qd,"b_day",	   b_day_8601	  );
    if (data_descr	&& *data_descr	   )
      xdbsql_querydef_setvar(qd,"descr",     data_descr     );
    if (data_photo_t	&& *data_photo_t	   )
      xdbsql_querydef_setvar(qd,"photo_type",data_photo_t   );
    if (data_photo_b	&& *data_photo_b	   )
      xdbsql_querydef_setvar(qd,"photo_bin" ,data_photo_b   );

    /* Execute the query! */
    querystring = xdbsql_querydef_finalize(qd);

    result = sqldb_query(self,querystring);

    xdbsql_querydef_free(qd);
    if (!result)
      log_error(NULL,"[xdbsql_vcard_set] query failed : %s",
		sqldb_error(self));
    else
      sqldb_free_result(result);

    return 1;  /* Qa'pla! */

} /* end xdbsql_vcard_set */

