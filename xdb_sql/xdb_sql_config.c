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

/* static tag names used in validation functions */
static const char bindvar_name[] = "bindvar";
static const char bindcol_name[] = "bindcol";
static const char name_attr_name[] = "name";

/* To add a namespace, in the old way (XML plus C) :
 * - check if existing validators can fit your queries
 *    - if not
 *	 - add new validator function
 *	 - add validator declaration below
 * - insert query name and validator ptr into s_query_table
 * - create a new source file with the set/get functions
 * - insert functions declarations in xdb_sql.h
 * - insert functions ptrs and namespace into static_modules
 * - insert source name in Makefile.am
 * - insert queries in the XML config file
 */

/* forward declarations for query table */
static int validate_auth_get(xmlnode q_root);
static int validate_auth_set(xmlnode q_root);
static int validate_simple_user(xmlnode q_root);
static int validate_last_set(xmlnode q_root);
static int validate_last_get(xmlnode q_root);
static int validate_load_roster_1(xmlnode q_root);
static int validate_load_roster_2(xmlnode q_root);
static int validate_add_roster_2(xmlnode q_root);
static int validate_registered_get(xmlnode q_root);
static int validate_registered_set(xmlnode q_root);
static int validate_add_roster_1(xmlnode q_root);
static int validate_spool(xmlnode q_root);
static int validate_despool(xmlnode q_root);
static int validate_vcard_get(xmlnode q_root);
static int validate_vcard_set(xmlnode q_root);
static int validate_filter_get(xmlnode q_root);
static int validate_filter_set(xmlnode q_root);
static int validate_roomconfig_get(xmlnode q_root);
static int validate_roomconfig_set(xmlnode q_root);
static int validate_simple_room(xmlnode q_root);
static int validate_roomoutcast_get(xmlnode q_root);
static int validate_roomoutcast_set(xmlnode q_root);

static const struct query_table s_query_table[] = {
	{"auth-get", NULL, validate_auth_get},
	{"auth-set", NULL, validate_auth_set},
	{"auth-disable", NULL, validate_simple_user},
	{"checkuser", NULL, validate_simple_user},
	{"authhash-get", NULL, validate_auth_get},
	{"authhash-set", NULL, validate_auth_set},
	{"authhash-disable", NULL, validate_simple_user},
	{"chekuserhash", NULL, validate_simple_user},
	{"auth-set-new", NULL, validate_simple_user},
	{"auth-set-new", NULL, validate_auth_set},
	{"authhash-set-new", NULL, validate_simple_user},
	{"authhash-set-new", NULL, validate_auth_set},
	{"last-set", NULL, validate_last_set},
	{"last-get", NULL, validate_last_get},
	{"last-purge", NULL, validate_simple_user},
	{"roster-load-1", NULL, validate_load_roster_1},
	{"roster-load-2", NULL, validate_load_roster_2},
	{"roster-purge-2", NULL, validate_simple_user},
	{"roster-add-2", NULL, validate_add_roster_2},
	{"register-get", NULL, validate_registered_get},
	{"register-remove", NULL, validate_simple_user},
	{"register-set", NULL, validate_registered_set},
	{"roster-purge-1", NULL, validate_simple_user},
	{"roster-add-1", NULL, validate_add_roster_1},
	{"spool", NULL, validate_spool},
	{"despool", NULL, validate_despool},
	{"spool-remove", NULL, validate_simple_user},
	{"vcard-get", NULL, validate_vcard_get},
	{"vcard-set", NULL, validate_vcard_set},
	{"vcard-remove", NULL, validate_simple_user},
	{"filter-get", NULL, validate_filter_get},
	{"filter-set", NULL, validate_filter_set},
	{"filter-remove", NULL, validate_simple_user},
	{"roomconfig-get", NULL, validate_roomconfig_get},
	{"roomconfig-set", NULL, validate_roomconfig_set},
	{"roomconfig-remove", NULL, validate_simple_room},
	{"roomoutcast-get", NULL, validate_roomoutcast_get},
	{"roomoutcast-set", NULL, validate_roomoutcast_set},
	{"roomoutcast-remove", NULL, validate_simple_room},
	{NULL, NULL, NULL}	/* SENTINEL */
};

/*
 * Bind a namespace to set/get functions.
 */
static const XdbSqlModule static_modules[] = {
	{NS_AUTH, xdbsql_auth_set, xdbsql_auth_get},
	{NS_AUTH_CRYPT, xdbsql_authhash_set, xdbsql_authhash_get},
	{NS_LAST, xdbsql_last_set, xdbsql_last_get},
	{NS_ROSTER, xdbsql_roster_set, xdbsql_roster_get},
	{NS_OFFLINE, xdbsql_offline_set, xdbsql_offline_get},
	{NS_REGISTER, xdbsql_register_set, xdbsql_register_get},
	{NS_VCARD, xdbsql_vcard_set, xdbsql_vcard_get},
	{NS_FILTER, xdbsql_filter_set, xdbsql_filter_get},
	{"muc:room:config", xdbsql_roomconfig_set, xdbsql_roomconfig_get},
	{"muc:list:outcast", xdbsql_roomoutcast_set, xdbsql_roomoutcast_get},
	{NULL, NULL, NULL}
};


static int handle_query_v1(XdbSqlDatas * self, xmlnode x);
static int handle_query_v2(XdbSqlDatas * self, xmlnode x);


/***********************************************************************
 * Query validation routines
 */

static int validate_auth_get(xmlnode q_root)
{
	xmlnode tmp;
	char *attr;
	int username_spec = 0;
	int password_spec = 0;

	for (tmp = xmlnode_get_firstchild(q_root); tmp; tmp = xmlnode_get_nextsibling(tmp)) {	/* check to make sure the proper data is present */
		if (j_strcmp(xmlnode_get_name(tmp), bindvar_name) == 0) {	/* get the "name" attribute and check it out */
			attr = xmlnode_get_attrib(tmp, name_attr_name);
			if (j_strcmp(attr, "user") == 0)
				username_spec = 1;
		} else if (j_strcmp(xmlnode_get_name(tmp), bindcol_name) == 0) {	/* get the "name" attribute and check it out */
			attr = xmlnode_get_attrib(tmp, name_attr_name);
			if (j_strcmp(attr, "password") == 0)
				password_spec = 1;

		}
		/* end else if */
	}			/* end for */

	return username_spec && password_spec;

}				/* end validate_auth_get */

static int validate_auth_set(xmlnode q_root)
{
	xmlnode tmp;
	char *attr;
	int username_spec = 0;
	int password_spec = 0;

	for (tmp = xmlnode_get_firstchild(q_root); tmp;
	     tmp = xmlnode_get_nextsibling(tmp))
		if (j_strcmp(xmlnode_get_name(tmp), bindvar_name) == 0) {	/* get the "name" attribute and check it out */
			attr = xmlnode_get_attrib(tmp, name_attr_name);
			if (j_strcmp(attr, "user") == 0)
				username_spec = 1;
			else if (j_strcmp(attr, "password") == 0)
				password_spec = 1;

		}
	/* end if and for */
	return username_spec && password_spec;

}				/* end validate_auth_set */

static int validate_simple_user(xmlnode q_root)
{
	xmlnode tmp;
	char *attr;

	for (tmp = xmlnode_get_firstchild(q_root); tmp;
	     tmp = xmlnode_get_nextsibling(tmp))
		if (j_strcmp(xmlnode_get_name(tmp), bindvar_name) == 0) {	/* get the "name" attribute and check it out */
			attr = xmlnode_get_attrib(tmp, name_attr_name);
			if (j_strcmp(attr, "user") == 0)
				return 1;

		}
	/* end if and for */
	return 0;

}				/* end validate_simple_user */

static int validate_last_set(xmlnode q_root)
{
	xmlnode tmp;
	char *buf;
	int username_spec = 0;
	int seconds_spec = 0;
	int state_spec = 0;

	for (tmp = xmlnode_get_firstchild(q_root); tmp; tmp = xmlnode_get_nextsibling(tmp)) {	/* check to make sure the proper data is present */
		if (j_strcmp(xmlnode_get_name(tmp), bindvar_name) == 0) {
			/* get the "name" attribute and check it out */
			buf = xmlnode_get_attrib(tmp, name_attr_name);
			if (j_strcmp(buf, "user") == 0)
				username_spec = 1;
			else if (j_strcmp(buf, "seconds") == 0)
				seconds_spec = 1;
			else if (j_strcmp(buf, "state") == 0)
				state_spec = 1;
		}

	}			/* end for */

	return (username_spec && seconds_spec && state_spec);
}				/* end validate_last_set */

static int validate_last_get(xmlnode q_root)
{
	xmlnode tmp;
	char *buf;
	int username_spec = 0;
	int seconds_spec = 0;
	int state_spec = 0;

	for (tmp = xmlnode_get_firstchild(q_root); tmp; tmp = xmlnode_get_nextsibling(tmp)) {	/* check to make sure the proper data is present */
		if (j_strcmp(xmlnode_get_name(tmp), bindvar_name) == 0) {
			/* get the "name" attribute and check it out */
			buf = xmlnode_get_attrib(tmp, name_attr_name);
			if (j_strcmp(buf, "user") == 0)
				username_spec = 1;
		} else if (j_strcmp(xmlnode_get_name(tmp), bindcol_name) ==
			   0) {
			buf = xmlnode_get_attrib(tmp, name_attr_name);
			if (j_strcmp(buf, "seconds") == 0)
				seconds_spec = 1;
			else if (j_strcmp(buf, "state") == 0)
				state_spec = 1;
		}

	}			/* end for */

	return (username_spec && seconds_spec && state_spec);
}				/* end validate_last_get */

static int validate_load_roster_1(xmlnode q_root)
{
	xmlnode tmp;
	char *attr;
	int user_spec = 0;
	int jid_spec = 0;
	int nickname_spec = 0;
	int subscription_spec = 0;
	int ask_spec = 0;
	int ext_spec = 0;

	for (tmp = xmlnode_get_firstchild(q_root); tmp; tmp = xmlnode_get_nextsibling(tmp)) {	/* look for both variable bindings and column bindings */
		if (j_strcmp(xmlnode_get_name(tmp), bindvar_name) == 0) {	/* get the variable name and check it */
			attr = xmlnode_get_attrib(tmp, name_attr_name);
			if (j_strcmp(attr, "user") == 0)
				user_spec = 1;

		} /* end if */
		else if (j_strcmp(xmlnode_get_name(tmp), bindcol_name) == 0) {	/* get the column name and check it */
			attr = xmlnode_get_attrib(tmp, name_attr_name);
			if (j_strcmp(attr, "jid") == 0)
				jid_spec = 1;
			else if (j_strcmp(attr, "nickname") == 0)
				nickname_spec = 1;
			else if (j_strcmp(attr, "subscription") == 0)
				subscription_spec = 1;
			else if (j_strcmp(attr, "ask") == 0)
				ask_spec = 1;
			else if (j_strcmp(attr, "x") == 0)
				ext_spec = 1;

		}
		/* end else if */
	}			/* end for */

	return user_spec && jid_spec && nickname_spec && subscription_spec
	    && ask_spec && ext_spec;

}				/* end validate_load_roster_1 */

static int validate_load_roster_2(xmlnode q_root)
{
	xmlnode tmp;
	char *attr;
	int user_spec = 0;
	int jid_spec = 0;
	int group_spec = 0;

	for (tmp = xmlnode_get_firstchild(q_root); tmp; tmp = xmlnode_get_nextsibling(tmp)) {	/* look for both variable bindings and column bindings */
		if (j_strcmp(xmlnode_get_name(tmp), bindvar_name) == 0) {	/* get the variable name and check it */
			attr = xmlnode_get_attrib(tmp, name_attr_name);
			if (j_strcmp(attr, "user") == 0)
				user_spec = 1;

		} /* end if */
		else if (j_strcmp(xmlnode_get_name(tmp), bindcol_name) == 0) {	/* get the column name and check it */
			attr = xmlnode_get_attrib(tmp, name_attr_name);
			if (j_strcmp(attr, "grp") == 0)
				group_spec = 1;
			else if (j_strcmp(attr, "jid") == 0)
				jid_spec = 1;

		}
		/* end else if */
	}			/* end for */

	return user_spec && jid_spec && group_spec;

}				/* end validate_load_roster_2 */

static int validate_add_roster_1(xmlnode q_root)
{
	xmlnode tmp;
	char *attr;
	int user_spec = 0;
	int jid_spec = 0;
	int nickname_spec = 0;
	int subscription_spec = 0;
	int ask_spec = 0;
	int ext_spec = 0;

	for (tmp = xmlnode_get_firstchild(q_root); tmp; tmp = xmlnode_get_nextsibling(tmp)) {	/* look for variable bindings */
		if (j_strcmp(xmlnode_get_name(tmp), bindvar_name) == 0) {	/* get the variable name and check it */
			attr = xmlnode_get_attrib(tmp, name_attr_name);
			if (j_strcmp(attr, "user") == 0)
				user_spec = 1;
			else if (j_strcmp(attr, "jid") == 0)
				jid_spec = 1;
			else if (j_strcmp(attr, "nickname") == 0)
				nickname_spec = 1;
			else if (j_strcmp(attr, "subscription") == 0)
				subscription_spec = 1;
			else if (j_strcmp(attr, "ask") == 0)
				ask_spec = 1;
			else if (j_strcmp(attr, "x") == 0)
				ext_spec = 1;

		}
		/* end if */
	}			/* end for */

	return user_spec && jid_spec && nickname_spec && subscription_spec
	    && ask_spec && ext_spec;

}				/* end validate_add_roster_1 */

static int validate_add_roster_2(xmlnode q_root)
{
	xmlnode tmp;
	char *attr;
	int user_spec = 0;
	int jid_spec = 0;
	int group_spec = 0;

	for (tmp = xmlnode_get_firstchild(q_root); tmp; tmp = xmlnode_get_nextsibling(tmp)) {	/* look for variable bindings */
		if (j_strcmp(xmlnode_get_name(tmp), bindvar_name) == 0) {	/* get the variable name and check it */
			attr = xmlnode_get_attrib(tmp, name_attr_name);
			if (j_strcmp(attr, "user") == 0)
				user_spec = 1;
			else if (j_strcmp(attr, "jid") == 0)
				jid_spec = 1;
			else if (j_strcmp(attr, "grp") == 0)
				group_spec = 1;

		}
		/* end if */
	}			/* end for */

	return user_spec && jid_spec && group_spec;

}				/* end validate_add_roster_2 */

static int validate_registered_get(xmlnode q_root)
{
	xmlnode tmp;
	int i;
	char *attr;
	char test[8];
	char *keys[7] = {
		"user",
		"login",
		"password",
		"name",
		"email",
		"stamp",
		"type"
	};

	memset(test, ' ', 7);
	test[7] = 0;

	for (tmp = xmlnode_get_firstchild(q_root); tmp;
	     tmp = xmlnode_get_nextsibling(tmp)) {
		attr = xmlnode_get_attrib(tmp, name_attr_name);

		if (j_strcmp(xmlnode_get_name(tmp), bindvar_name) == 0) {
			if (j_strcmp(attr, keys[0]) == 0)
				test[0] = 'X';
		} else
			for (i = 1; i < 7; i++)
				if (j_strcmp
				    (xmlnode_get_name(tmp),
				     bindcol_name) == 0)
					if (j_strcmp(attr, keys[i]) == 0)
						test[i] = 'X';
	}			/* end for */

	return (strncmp(test, "XXXXXXX", 7) == 0);
}				/* end validate_registered_get */

static int validate_registered_set(xmlnode q_root)
{
	xmlnode tmp;
	int i;
	char *attr;
	char test[8];
	char *keys[7] = {
		"user",
		"login",
		"password",
		"name",
		"email",
		"stamp",
		"type"
	};

	memset(test, ' ', 7);
	test[7] = 0;

	for (tmp = xmlnode_get_firstchild(q_root); tmp;
	     tmp = xmlnode_get_nextsibling(tmp)) {
		attr = xmlnode_get_attrib(tmp, name_attr_name);
		for (i = 0; i < 7; i++)
			if (j_strcmp(xmlnode_get_name(tmp), bindvar_name)
			    == 0)
				if (j_strcmp(attr, keys[i]) == 0)
					test[i] = 'X';
	}			/* end for */

	return (strncmp(test, "XXXXXXX", 7) == 0);

}				/* end validate_registered_set */

static int validate_spool(xmlnode q_root)
{
	xmlnode tmp;
	char *attr;
	char test[11];

	memset(test, ' ', 10);
	test[0] = 'X';
	test[10] = '\0';
	for (tmp = xmlnode_get_firstchild(q_root); tmp;
	     tmp = xmlnode_get_nextsibling(tmp))
		if (j_strcmp(xmlnode_get_name(tmp), bindvar_name) == 0) {	/* get the "name" attribute and check it out */
			attr = xmlnode_get_attrib(tmp, name_attr_name);
			if (j_strcmp(attr, "to") == 0)
				test[1] = 'X';
			else if (j_strcmp(attr, "from") == 0)
				test[2] = 'X';
			else if (j_strcmp(attr, "id") == 0)
				test[3] = 'X';
			else if (j_strcmp(attr, "priority") == 0)
				test[4] = 'X';
			else if (j_strcmp(attr, "type") == 0)
				test[5] = 'X';
			else if (j_strcmp(attr, "thread") == 0)
				test[6] = 'X';
			else if (j_strcmp(attr, "subject") == 0)
				test[7] = 'X';
			else if (j_strcmp(attr, "body") == 0)
				test[8] = 'X';
			else if (j_strcmp(attr, "x") == 0)
				test[9] = 'X';

		}
	/* end if and for */
	return (strcmp(test, "XXXXXXXXXX") == 0);

}				/* end validate_spool */

static int validate_despool(xmlnode q_root)
{
	xmlnode tmp;
	char *attr;
	char test[11];

	memset(test, ' ', 10);
	test[10] = '\0';
	for (tmp = xmlnode_get_firstchild(q_root); tmp; tmp = xmlnode_get_nextsibling(tmp)) {	/* test for the presence of all required bindings */
		if (j_strcmp(xmlnode_get_name(tmp), bindvar_name) == 0) {	/* get the "name" attribute and check it out */
			attr = xmlnode_get_attrib(tmp, name_attr_name);
			if (j_strcmp(attr, "user") == 0)
				test[0] = 'X';

		} /* end if */
		else if (j_strcmp(xmlnode_get_name(tmp), bindcol_name) == 0) {	/* get the "name" attribute and check it out */
			attr = xmlnode_get_attrib(tmp, name_attr_name);
			if (j_strcmp(attr, "to") == 0)
				test[1] = 'X';
			else if (j_strcmp(attr, "from") == 0)
				test[2] = 'X';
			else if (j_strcmp(attr, "id") == 0)
				test[3] = 'X';
			else if (j_strcmp(attr, "priority") == 0)
				test[4] = 'X';
			else if (j_strcmp(attr, "type") == 0)
				test[5] = 'X';
			else if (j_strcmp(attr, "thread") == 0)
				test[6] = 'X';
			else if (j_strcmp(attr, "subject") == 0)
				test[7] = 'X';
			else if (j_strcmp(attr, "body") == 0)
				test[8] = 'X';
			else if (j_strcmp(attr, "x") == 0)
				test[9] = 'X';

		}
		/* end else if */
	}			/* end for */

	return (strcmp(test, "XXXXXXXXXX") == 0);

}				/* end validate_despool */

static int validate_vcard_get(xmlnode q_root)
{
	xmlnode tmp;
	int i;
	char *attr;
	char test[23];
	char *keys[22] = {
		"user",
		"full_name",
		"first_name",
		"last_name",
		"nick_name",
		"url",
		"address1",
		"address2",
		"locality",
		"region",
		"pcode",
		"country",
		"telephone",
		"email",
		"orgname",
		"orgunit",
		"title",
		"role",
		"b_day",
		"descr",
		"photo_type",
		"photo_bin"
	};

	memset(test, ' ', 22);
	test[22] = 0;

	for (tmp = xmlnode_get_firstchild(q_root); tmp;
	     tmp = xmlnode_get_nextsibling(tmp)) {
		attr = xmlnode_get_attrib(tmp, name_attr_name);

		if (j_strcmp(xmlnode_get_name(tmp), bindvar_name) == 0) {
			if (j_strcmp(attr, keys[0]) == 0)
				test[0] = 'X';
		} else
			for (i = 1; i < 22; i++)
				if (j_strcmp
				    (xmlnode_get_name(tmp),
				     bindcol_name) == 0)
					if (j_strcmp(attr, keys[i]) == 0)
						test[i] = 'X';
	}			/* end for */

	return (strncmp(test, "XXXXXXXXXXXXXXXXXXXXXX", 22) == 0);
}				/* end validate_vcard_get */

static int validate_vcard_set(xmlnode q_root)
{
	xmlnode tmp;
	int i;
	char *attr;
	char test[23];
	char *keys[22] = {
		"user",
		"full_name",
		"first_name",
		"last_name",
		"nick_name",
		"url",
		"address1",
		"address2",
		"locality",
		"region",
		"pcode",
		"country",
		"telephone",
		"email",
		"orgname",
		"orgunit",
		"title",
		"role",
		"b_day",
		"descr",
		"photo_type",
		"photo_bin"
	};

	memset(test, ' ', 22);
	test[22] = 0;

	for (tmp = xmlnode_get_firstchild(q_root); tmp;
	     tmp = xmlnode_get_nextsibling(tmp)) {
		attr = xmlnode_get_attrib(tmp, name_attr_name);
		for (i = 0; i < 22; i++)
			if (j_strcmp(xmlnode_get_name(tmp), bindvar_name)
			    == 0)
				if (j_strcmp(attr, keys[i]) == 0)
					test[i] = 'X';
	}			/* end for */

	return (strncmp(test, "XXXXXXXXXXXXXXXXXXXXXX", 22) == 0);

}				/* end validate_vcard_set */

static int validate_filter_get(xmlnode q_root)
{
	xmlnode tmp;
	int i;
	char *attr;
	char test[13];
	char *keys[13] = {
		"user",
		"unavailable",
		"from",
		"resource",
		"subject",
		"body",
		"show",
		"type",
		"offline",
		"forward",
		"reply",
		"continue",
		"settype"
	};

	memset(test, ' ', 13);
	test[13] = 0;

	for (tmp = xmlnode_get_firstchild(q_root); tmp;
	     tmp = xmlnode_get_nextsibling(tmp)) {
		attr = xmlnode_get_attrib(tmp, name_attr_name);
		if (j_strcmp(xmlnode_get_name(tmp), bindvar_name) == 0) {
			if (j_strcmp(attr, keys[0]) == 0)
				test[0] = 'X';
		} else
			for (i = 1; i < 13; i++)
				if (j_strcmp
				    (xmlnode_get_name(tmp),
				     bindcol_name) == 0)
					if (j_strcmp(attr, keys[i]) == 0)
						test[i] = 'X';
	}			/* end for */
	return (strncmp(test, "XXXXXXXXXXXXX", 13) == 0);
}				/* end validate_filter_get */

static int validate_filter_set(xmlnode q_root)
{
	xmlnode tmp;
	int i;
	char *attr;
	char test[13];
	char *keys[13] = {
		"user",
		"unavailable",
		"from",
		"resource",
		"subject",
		"body",
		"show",
		"type",
		"offline",
		"forward",
		"reply",
		"continue",
		"settype"
	};

	memset(test, ' ', 13);
	test[13] = 0;

	for (tmp = xmlnode_get_firstchild(q_root); tmp;
	     tmp = xmlnode_get_nextsibling(tmp)) {
		attr = xmlnode_get_attrib(tmp, name_attr_name);
		for (i = 0; i < 13; i++)
			if (j_strcmp(xmlnode_get_name(tmp), bindvar_name)
			    == 0)
				if (j_strcmp(attr, keys[i]) == 0)
					test[i] = 'X';
	}			/* end for */

	return (strncmp(test, "XXXXXXXXXXXXX", 13) == 0);

}				/* end validate_filter_set */

static int validate_roomconfig_get(xmlnode q_root)
{
	xmlnode tmp;
	int i;
	char *attr;
	char test[23];
	char *keys[22] = {
		"room",
		"owner",
		"name",
		"secret",
		"private",
		"leave",
		"join",
		"rename",
		"public",
		"subjectlock",
		"maxusers",
		"moderated",
		"defaultype",
		"privmsg",
		"invitation",
		"invites",
		"legacy",
		"visible",
		"logformat",
		"logging",
		"description",
		"persistant"
	};

	memset(test, ' ', 22);
	test[22] = 0;

	for (tmp = xmlnode_get_firstchild(q_root); tmp;
	     tmp = xmlnode_get_nextsibling(tmp)) {
		attr = xmlnode_get_attrib(tmp, name_attr_name);

		if (j_strcmp(xmlnode_get_name(tmp), bindvar_name) == 0) {
			if (j_strcmp(attr, keys[0]) == 0)
				test[0] = 'X';
		} else
			for (i = 1; i < 22; i++)
				if (j_strcmp
				    (xmlnode_get_name(tmp),
				     bindcol_name) == 0)
					if (j_strcmp(attr, keys[i]) == 0)
						test[i] = 'X';
	}

	return (strncmp(test, "XXXXXXXXXXXXXXXXXXXXXX", 22) == 0);
}

static int validate_roomconfig_set(xmlnode q_root)
{
	xmlnode tmp;
	int i;
	char *attr;
	char test[23];
	char *keys[22] = {
		"room",
		"owner",
		"name",
		"secret",
		"private",
		"leave",
		"join",
		"rename",
		"public",
		"subjectlock",
		"maxusers",
		"moderated",
		"defaultype",
		"privmsg",
		"invitation",
		"invites",
		"legacy",
		"visible",
		"logformat",
		"logging",
		"description",
		"persistant"
	};

	memset(test, ' ', 22);
	test[22] = 0;

	for (tmp = xmlnode_get_firstchild(q_root); tmp;
	     tmp = xmlnode_get_nextsibling(tmp)) {
		attr = xmlnode_get_attrib(tmp, name_attr_name);

		for (i = 0; i < 22; i++)
			if (j_strcmp(xmlnode_get_name(tmp), bindvar_name)
			    == 0)
				if (j_strcmp(attr, keys[i]) == 0)
					test[i] = 'X';
	}

	return (strncmp(test, "XXXXXXXXXXXXXXXXXXXXXX", 22) == 0);
}


static int validate_simple_room(xmlnode q_root)
{
	xmlnode tmp;
	char *attr;

	for (tmp = xmlnode_get_firstchild(q_root); tmp;
	     tmp = xmlnode_get_nextsibling(tmp))
		if (j_strcmp(xmlnode_get_name(tmp), bindvar_name) == 0) {
			attr = xmlnode_get_attrib(tmp, name_attr_name);
			if (j_strcmp(attr, "room") == 0)
				return 1;
		}

	return 0;
}

static int validate_roomoutcast_get(xmlnode q_root)
{
	xmlnode tmp;
	int i;
	char *attr;
	char test[6];
	char *keys[5] = {
		"room",
		"userid",
		"reason",
		"responsibleid",
		"responsiblenick"
	};

	memset(test, ' ', 5);
	test[5] = 0;

	for (tmp = xmlnode_get_firstchild(q_root); tmp;
	     tmp = xmlnode_get_nextsibling(tmp)) {
		attr = xmlnode_get_attrib(tmp, name_attr_name);

		if (j_strcmp(xmlnode_get_name(tmp), bindvar_name) == 0) {
			if (j_strcmp(attr, keys[0]) == 0)
				test[0] = 'X';
		} else
			for (i = 1; i < 5; i++)
				if (j_strcmp
				    (xmlnode_get_name(tmp),
				     bindcol_name) == 0)
					if (j_strcmp(attr, keys[i]) == 0)
						test[i] = 'X';
	}

	return (strncmp(test, "XXXXX", 5) == 0);
}

static int validate_roomoutcast_set(xmlnode q_root)
{
	xmlnode tmp;
	int i;
	char *attr;
	char test[6];
	char *keys[5] = {
		"room",
		"userid",
		"reason",
		"responsibleid",
		"responsiblenick"
	};

	memset(test, ' ', 5);
	test[5] = 0;

	for (tmp = xmlnode_get_firstchild(q_root); tmp;
	     tmp = xmlnode_get_nextsibling(tmp)) {
		attr = xmlnode_get_attrib(tmp, name_attr_name);

		for (i = 0; i < 5; i++)
			if (j_strcmp(xmlnode_get_name(tmp), bindvar_name)
			    == 0)
				if (j_strcmp(attr, keys[i]) == 0)
					test[i] = 'X';
	}

	return (strncmp(test, "XXXXX", 5) == 0);
}


/***********************************************************************
 * External functions
 */

int is_true(const char *str)
{
	static const char *test[] = { "true", "yes", "on", "1", NULL };
	register int i;

	if (!str)
		return 0;	/* can't be true */
	for (i = 0; test[i]; i++)
		if (j_strcmp(str, test[i]) == 0)
			return 1;	/* match found */

	return 0;		/* no match */

}				/* end is_true */

int is_false(const char *str)
{
	static const char *test[] = { "false", "no", "off", "0", NULL };
	register int i;

	if (!str)
		return 0;	/* can't be false */
	for (i = 0; test[i]; i++)
		if (j_strcmp(str, test[i]) == 0)
			return 1;	/* match found */

	return 0;		/* no match */

}				/* end is_false */

int xdbsql_config_init(XdbSqlDatas * self, xmlnode cfgroot)
{
	static const char def_hostname[] = "localhost";	/* default host name */
	static const char def_portstr[] = "0";	/* default port */
	static const char def_user_db[] = "jabber";	/* default user and database name */
	static const char def_pass[] = "";	/* default password */
	xmlnode conn_base, query_base, backend, tmp;	/* pointers into the node tree */
	const char *hostname = NULL;	/* SQL host name */
	const char *portstr = NULL;	/* SQL port number */
	const char *user = NULL;	/* SQL username */
	const char *password = NULL;	/* SQL password */
	const char *database = NULL;	/* SQL database name */

	if (!cfgroot)
		return 0;	/* configuration not present */

	/* Find backend */
	backend = xmlnode_get_tag(cfgroot, "backend");
	if (backend) {
		const char *drvname =
		    (const char *)
		    xmlnode_get_data(xmlnode_get_firstchild(backend));
		if (!drvname) {
			log_error(NULL,
				  "[xdbsql_config_init] no backend specified");
			return 0;
		}
		if (!xdbsql_backend_load(self, drvname)) {
			log_error(NULL,
				  "[xdbsql_config_init] cannot load %s backend",
				  drvname);
			return 0;
		}
	} else {
		log_error(NULL,
			  "[xdbsql_config_init] no backend specified");
		return 0;
	}

	/* Find sleep */
	self->sleep_debug = j_atoi(xmlnode_get_tag_data(cfgroot, "sleep-debug"), 0);
	if (self->sleep_debug < 0)
		self->sleep_debug = -self->sleep_debug;

	self->timeout = j_atoi(xmlnode_get_tag_data(cfgroot, "timeout"), 10);
	
	/* Have a look in the XML configuration data for connection information. */
	conn_base = xmlnode_get_tag(cfgroot, "connection");
	if (conn_base) {	/* get the database connection parameters from the <connection> block */
		tmp = xmlnode_get_tag(conn_base, "host");
		if (tmp)
			hostname =
			    (const char *)
			    xmlnode_get_data(xmlnode_get_firstchild(tmp));
		tmp = xmlnode_get_tag(conn_base, "port");
		if (tmp)
			portstr =
			    (const char *)
			    xmlnode_get_data(xmlnode_get_firstchild(tmp));
		tmp = xmlnode_get_tag(conn_base, "user");
		if (tmp)
			user =
			    (const char *)
			    xmlnode_get_data(xmlnode_get_firstchild(tmp));
		tmp = xmlnode_get_tag(conn_base, "pass");
		if (tmp)
			password =
			    (const char *)
			    xmlnode_get_data(xmlnode_get_firstchild(tmp));
		tmp = xmlnode_get_tag(conn_base, "db");
		if (tmp)
			database =
			    (const char *)
			    xmlnode_get_data(xmlnode_get_firstchild(tmp));

	}

	/* end if */
	/* fill in defaults for the connection parameters as necessary */
	if (!hostname)
		hostname = def_hostname;
	if (!portstr)
		portstr = def_portstr;
	if (!user)
		user = def_user_db;
	if (!password)
		password = def_pass;
	if (!database)
		database = def_user_db;

	self->modules = (struct XdbSqlModule *) pmalloc(self->poolref, sizeof(static_modules));
	memcpy(self->modules, static_modules, sizeof(static_modules));

	self->query_table = (struct query_table *) pmalloc(self->poolref, sizeof(s_query_table));
	memcpy(self->query_table, s_query_table, sizeof(s_query_table));

	/* look at the queries and load them into our query table */
	query_base = xmlnode_get_tag(cfgroot, "queries");
	if (query_base) {
		/* begin probing for queries in the query base section */
		for (tmp = xmlnode_get_firstchild(query_base); tmp;
		     tmp = xmlnode_get_nextsibling(tmp)) {
			if ((j_strcmp(xmlnode_get_name(tmp), "querydef") == 0)
			    && xmlnode_get_tag(tmp, "text")) {
				if (!xmlnode_get_attrib(tmp, "dtd")) {
					if (!handle_query_v1(self, tmp))
						return 0;
				} else
				    if (j_strcmp(xmlnode_get_attrib(tmp, "dtd"), "2") == 0) {
					if (!handle_query_v2(self, tmp))
						return 0;
				} else {
					log_error(NULL,
						  "[xdbsql_config_init]"
						  " query with invalid 'dtd' value");
					return 0;
				}
			}
		}
	}
	/* end if */
	if (!sqldb_connect(self, hostname, portstr, user, password, database)) {
		/* the database is unavailable - we are probably f**ked no matter what we do */
		log_error(NULL,
			  "[xdbsql_config_init] cannot connect database : %s\n",
			  sqldb_error(self));
		return 0;
	}
	return 1;		/* Qa'pla! */

}				/* end xdbsql_config_init */


static int handle_query_v1(XdbSqlDatas * self, xmlnode x)
{
	char *q_name;		/* query name */
	int i;

	/* due to a validate error, temp comment this code ! */
	/* by BO, 2001/10/16 */

	/* get the query name and check it against the list */
	q_name = xmlnode_get_attrib(x, name_attr_name);
	for (i = 0; self->query_table[i].query_name; i++)
		if (j_strcmp(self->query_table[i].query_name, q_name) == 0) {	/* now run the validation function */
			if ((*(self->query_table[i].validate_func)) (x)) {
				self->query_table[i].query = x;
			} else {	/* query validation failed - this is an error! */
				log_error(NULL,
					  "[xdbsql_config_init] query %s validate failed",
					  q_name);
				return 0;
			}	/* end else */
			break;

		}		/* end if and for */
	return 1;
}

/* each qd has its own pool, which contains the list node used
 * in self->queries_v2.
 */
static int handle_query_v2(XdbSqlDatas * self, xmlnode x)
{
	pool p;
	query_def qd;
	query_node ql;

	p = pool_new();
	qd = xdbsql_querydef_init_v2(self, p, x);
	if (!qd) {
		pool_free(p);
		return 0;
	}

	ql = pmalloc(p, sizeof(*ql));
	ql->query_name = xmlnode_get_attrib(x, name_attr_name);
	ql->qd = qd;

	/* insert at the beginning of the v2 queries list */
	ql->next = self->queries_v2;
	self->queries_v2 = ql;

	return 1;
}

xmlnode xdbsql_query_get(XdbSqlDatas * self, const char *name)
{
	register int i;		/* loop counter */

	/* do a simple linear search */
	for (i = 0; self->query_table[i].query_name; i++)
		if (j_strcmp(self->query_table[i].query_name, name) == 0)
			return self->query_table[i].query;
	return NULL;		/* not found */

}				/* end xdbsql_query_get */



/*
 * ???
 */
void xdbsql_config_uninit(XdbSqlDatas * self)
{
	register int i;		/* loop counter */

	/*
	 * close connection.
	 */
	sqldb_disconnect(self);

	xdbsql_backend_destroy(self);

	/* null out the predefined query table */
	for (i = 0; self->query_table[i].query_name; i++)
		self->query_table[i].query = NULL;

}				/* end xdbsql_config_uninit */
