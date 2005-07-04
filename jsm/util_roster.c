/* --------------------------------------------------------------------------
 *
 * License
 *
 * The contents of this file are subject to the Jabber Open Source License
 * Version 1.0 (the "JOSL").  You may not copy or use this file, in either
 * source code or executable form, except in compliance with the JOSL. You
 * may obtain a copy of the JOSL at http://www.jabber.org/ or at
 * http://www.opensource.org/.
 *
 * Software distributed under the JOSL is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied.  See the JOSL
 * for the specific language governing rights and limitations under the
 * JOSL.
 *
 * Copyrights
 *
 * Portions created by or assigned to Jabber.com, Inc. are
 * Copyright (c) 1999-2002 Jabber.com, Inc.  All Rights Reserved.  Contact
 * information for Jabber.com, Inc. is available at http://www.jabber.com/.
 *
 * Portions Copyright (c) 1998-1999 Jeremie Miller.
 *
 * Acknowledgements
 *
 * Special thanks to the Jabber Open Source Contributors for their
 * suggestions and support of Jabber.
 *
 * Alternatively, the contents of this file may be used under the terms of the
 * GNU General Public License Version 2 or later (the "GPL"), in which case
 * the provisions of the GPL are applicable instead of those above.  If you
 * wish to allow use of your version of this file only under the terms of the
 * GPL and not to allow others to use your version of this file under the JOSL,
 * indicate your decision by deleting the provisions above and replace them
 * with the notice and other provisions required by the GPL.  If you do not
 * delete the provisions above, a recipient may use your version of this file
 * under either the JOSL or the GPL.
 *
 *
 * util.c -- utility functions for jsm
 *
 * --------------------------------------------------------------------------*/

/*
   Developed by £ukasz Karwacki <lukasm@wp-sa.pl>


   * we must use sem here because of NS_LAST and NS_BROWSE modules
   where js_trust is called offline. Also roster save is called in
   mod_roster in e_DELIVER handler.
*/

#include "jsm.h"

typedef struct js_roster_group_struct {
	char *name;
	struct js_roster_group_struct *next;
} *js_roster_group, _js_roster_group;

#define SUBS_NONE 0
#define SUBS_TO   1
#define SUBS_FROM 2
#define SUBS_BOTH 3

#define TYPE_USER  0		/* user@server           */
#define TYPE_SERVER    1	/* server                */
#define TYPE_USER_RESOURCE 2	/* user@server/resource  */
#define TYPE_SERVER_RESOURCE 3	/* server/resource       */

typedef struct js_roster_struct {
	char *user;
	char *server;
	short type;
	short subscription;
	js_roster_group gr;
	struct js_roster_struct *next;
} *js_roster, _js_roster;

#define NO_ROSTER_CACHE (void*)-1

/* ****************************************************************** */
js_roster add_roster_cache_item(udata u, jid id, int subscription)
{
	js_roster cur;

	/* check if he exists */
	for (cur = (js_roster) u->roster_cache; cur; cur = cur->next) {
		/* user */
		switch (cur->type) {
		case TYPE_USER:
			if (!id->user)
				continue;
			if (strcasecmp(cur->user, id->user) != 0)
				continue;
			if (strcmp(cur->server, id->server) != 0)
				continue;
			break;
		case TYPE_SERVER_RESOURCE:
			if (id->user)
				continue;
			if (!id->resource)
				continue;
			if (strcmp(cur->server, id->server) != 0)
				continue;
			/* resource */
			if (strcmp(cur->user, id->resource) != 0)
				continue;
			break;
		case TYPE_SERVER:
			if (id->user)
				continue;
			if (id->resource)
				continue;
			if (strcmp(cur->server, id->server) != 0)
				continue;
			break;
		case TYPE_USER_RESOURCE:
			if (!id->user)
				continue;
			if (!id->resource)
				continue;
			/* XXX  */
			break;
		}

		/* found existing */
		log_debug("found existing");
		if (cur->subscription >= 100)
			cur->subscription -= 100;
		break;
	}

	if (cur == NULL) {
		cur = pmalloco(u->p, sizeof(_js_roster));
		if (id->user) {
			/* XXX check for resource */
			cur->user = pstrdup(u->p, id->user);
			cur->server = pstrdup(u->p, id->server);
			cur->type = TYPE_USER;
		} else {
			cur->server = pstrdup(u->p, id->server);
			if (id->resource) {
				cur->user = pstrdup(u->p, id->resource);
				cur->type = TYPE_SERVER_RESOURCE;
			} else
				cur->type = TYPE_SERVER;
		}

		cur->next = (js_roster) u->roster_cache;
		u->roster_cache = cur;

		log_debug("add");
	}

	/* now we have cur */
	cur->subscription = subscription;

	if (cur->type == TYPE_SERVER) {
		log_debug("Item user NONE server %s subs %d",
			  cur->server, cur->subscription);
	} else {
		log_debug("Item type %d user/res %s server %s subs %d",
			  cur->type, cur->user, cur->server,
			  cur->subscription);
	}

	return cur;
}

/* adds groups to roster, also	*/
void add_roster_item_group(udata u, js_roster cur, js_roster_group old_gr,
			   char *name)
{
	js_roster_group temp_gr, cur_gr;

	if (name == NULL)
		return;

	/* find existing one */
	for (cur_gr = old_gr; cur_gr; cur_gr = cur_gr->next) {
		if (strcmp(cur_gr->name, name) == 0)
			break;
	}

	/* check if existing one is not already in cur item */
	for (temp_gr = cur->gr; temp_gr; temp_gr = temp_gr->next) {
		if (cur_gr == temp_gr) {
			cur_gr = NULL;
			break;
		}
	}

	/* add new if didn't found exisitng one */
	if (cur_gr == NULL) {
		log_debug("alloc new group");
		cur_gr = pmalloco(u->p, sizeof(_js_roster_group));
		cur_gr->name = pstrdup(u->p, name);
	}

	log_debug("group %s add", name);

	/* add */
	cur_gr->next = cur->gr;
	cur->gr = cur_gr;
}

/* ****************************************************************** */
void ReBuildRosterCache(udata u, xmlnode roster)
{
	xmlnode cur, gr;
	jid id;
	pool p;
	int subscription;
	js_roster ros, prev;
	js_roster_group old_gr;

	/* remove cache , mark no items */
	if (roster == NULL) {
		log_debug("No roster items");
		u->roster_cache = NO_ROSTER_CACHE;
		return;
	}

	/* lock cache building, offline js_trust request
	   can be executed in many threads */
	SEM_LOCK(u->sem);

	if (u->roster_cache == NO_ROSTER_CACHE) {
		u->roster_cache = NULL;
	}

	/* mark for delete */
	if (u->roster_cache) {
		for (ros = (js_roster) u->roster_cache; ros;
		     ros = ros->next)
			ros->subscription += 100;
	}

	/* parse roster */
	p = xmlnode_pool(roster);
	for (cur = xmlnode_get_firstchild(roster); cur;
	     cur = xmlnode_get_nextsibling(cur)) {
		id = jid_new(p, xmlnode_get_attrib(cur, "jid"));
		if (id == NULL)
			continue;

#ifdef FORWP
		/* do not cache GG */
		if (j_strcmp(id->server, "gg.jabber.wp.pl") == 0)
			continue;
#endif

		if (j_strcmp(xmlnode_get_attrib(cur, "subscription"), "to")
		    == 0)
			subscription = SUBS_TO;
		else if (j_strcmp
			 (xmlnode_get_attrib(cur, "subscription"),
			  "from") == 0)
			subscription = SUBS_FROM;
		else if (j_strcmp
			 (xmlnode_get_attrib(cur, "subscription"),
			  "both") == 0)
			subscription = SUBS_BOTH;
		else
			subscription = SUBS_NONE;

		/* add item */
		ros = add_roster_cache_item(u, id, subscription);

		old_gr = ros->gr;
		ros->gr = NULL;

		for (gr = xmlnode_get_firstchild(cur); gr;
		     gr = xmlnode_get_nextsibling(gr)) {

			if (xmlnode_get_type(gr) != NTYPE_TAG)
				continue;
			if (j_strcmp(xmlnode_get_name(gr), "group") != 0)
				continue;

			/* add group */
			add_roster_item_group(u, ros, old_gr,
					      xmlnode_get_data(gr));
		}
	}

	/* remove deleted */
	if (u->roster_cache) {
		while (((js_roster) u->roster_cache) &&
		       (((js_roster) u->roster_cache)->subscription >=
			100)) {
			log_debug("remove first");
			u->roster_cache =
			    ((js_roster) u->roster_cache)->next;
		}

		if (u->roster_cache) {
			prev = (js_roster) u->roster_cache;
			for (ros = ((js_roster) u->roster_cache)->next;
			     ros; ros = ros->next)
				if (ros->subscription >= 100) {
					log_debug("remove");
					prev->next = ros->next;
				}
		}
	}

	/* if removed all */
	if (u->roster_cache == NULL) {
		u->roster_cache = NO_ROSTER_CACHE;
	}

	SEM_UNLOCK(u->sem);
}

/* ****************************************************************** */
/* 0 - empty cache, 1 cache oki */
int load_cache_roster(udata u)
{
	xmlnode roster;

	if (u->roster_cache == NO_ROSTER_CACHE)
		return 0;

	if (u->roster_cache != NULL)
		return 1;

	log_debug("roster load from XDB");

	roster = xdb_get(u->si->xc, u->id, NS_ROSTER);
	ReBuildRosterCache(u, roster);
	xmlnode_free(roster);

	if (u->roster_cache == NO_ROSTER_CACHE)
		return 0;

	return 1;
}

/* ****************************************************************** */

/* this tries to be a smarter jid matcher, where a "host" matches any "user@host" and "user@host" matches "user@host/resource" */
int _js_jidscanner(jid id, jid match)
{
	for (; id != NULL; id = id->next) {
		if (j_strcmp(id->server, match->server) != 0)
			continue;
		if (id->user == NULL)
			return 1;
		if (j_strcasecmp(id->user, match->user) != 0)
			continue;
		if (id->resource == NULL)
			return 1;
		if (j_strcmp(id->resource, match->resource) != 0)
			continue;
		return 1;
	}
	return 0;
}

/* return true if user is in roster with from or both subscription */
int js_istrusted(udata u, jid id)
{
	js_roster cur;

	if (jid_cmpx(u->id, id, JID_USER | JID_SERVER) == 0)
		return 1;

	if (load_cache_roster(u) == 0)
		return 0;

	for (cur = (js_roster) u->roster_cache; cur; cur = cur->next) {
		/* user */
		switch (cur->type) {
		case TYPE_USER:
			if (!id->user)
				continue;
			if (strcasecmp(cur->user, id->user) != 0)
				continue;
			if (strcmp(cur->server, id->server) != 0)
				continue;
			break;
		case TYPE_SERVER_RESOURCE:
			if (id->user)
				continue;
			//      if (!id->resource) continue;
			if (strcmp(cur->server, id->server) != 0)
				continue;
			if (!id->resource)
				break;
			/* resource */
			if (strcmp(cur->user, id->resource) != 0)
				continue;
			break;
		case TYPE_SERVER:
			if (id->user)
				continue;
			//      if (id->resource) continue;
			if (strcmp(cur->server, id->server) != 0)
				continue;
			break;
		case TYPE_USER_RESOURCE:
			if (!id->user)
				continue;
			if (!id->resource)
				continue;

			/* XXX */
			break;
		}

		if (cur->subscription == SUBS_FROM
		    || cur->subscription == SUBS_BOTH)
			return 1;
		else
			return 0;
	}
	return 0;
}

/* returns true if id is trusted for this user */
int js_trust(udata u, jid id)
{
	js_roster cur;
	if (u == NULL || id == NULL)
		return 0;

	/* first, check global trusted ids */
	if (_js_jidscanner(u->si->gtrust, id))
		return 1;

	if (jid_cmpx(u->id, id, JID_USER | JID_SERVER) == 0)
		return 1;

	if (load_cache_roster(u) == 0)
		return 0;

	for (cur = (js_roster) u->roster_cache; cur; cur = cur->next) {
		/* user */
		switch (cur->type) {
		case TYPE_USER:
			if (!id->user)
				continue;
			if (strcasecmp(cur->user, id->user) != 0)
				continue;
			if (strcmp(cur->server, id->server) != 0)
				continue;
			break;
		case TYPE_SERVER_RESOURCE:
			if (id->user)
				continue;
			if (strcmp(cur->server, id->server) != 0)
				continue;
			if (!id->resource)
				break;
			/* resource */
			if (strcmp(cur->user, id->resource) != 0)
				continue;
			break;
		case TYPE_SERVER:
			if (id->user)
				continue;
			//      if (id->resource) continue;
			if (strcmp(cur->server, id->server) != 0)
				continue;
			break;
		case TYPE_USER_RESOURCE:
			if (!id->user)
				continue;
			if (!id->resource)
				continue;

			/* XXX */
			break;
		}

		if (cur->subscription == SUBS_FROM
		    || cur->subscription == SUBS_BOTH)
			return 1;
		else
			return 0;
	}
	return 0;
}

/* use this to save roster to handle roster changes */
void js_roster_save(udata u, xmlnode roster)
{
	log_debug("roster save");
	ReBuildRosterCache(u, roster);
	xdb_set(u->si->xc, u->id, NS_ROSTER, roster);
}

/* this function is written specially for mod_presence */
void js_trust_populate_presence(mapi m, jid notify)
{
	js_roster cur;
	char buf[501];

	if (load_cache_roster(m->user) == 0)
		return;

	for (cur = (js_roster) m->user->roster_cache; cur; cur = cur->next) {
		/* build jid */
		jid id = NULL;
		switch (cur->type) {
		case TYPE_USER:
			snprintf(buf, 500, "%s@%s", cur->user,
				 cur->server);
			id = jid_new(m->packet->p, buf);
			break;
		case TYPE_SERVER_RESOURCE:
			snprintf(buf, 500, "%s/%s", cur->server,
				 cur->user);
			id = jid_new(m->packet->p, buf);
			break;
		case TYPE_SERVER:
			id = jid_new(m->packet->p, cur->server);
			break;
		case TYPE_USER_RESOURCE:
			continue;
			/* XXX */
			break;
		}

		log_debug("FOUND %s", jid_full(id));

		if (cur->subscription == SUBS_TO
		    || cur->subscription == SUBS_BOTH) {
			xmlnode pnew;
			log_debug("we're new here, probe them");
			pnew =
			    jutil_presnew(JPACKET__PROBE, jid_full(id),
					  NULL);
			//      xmlnode_put_attrib(pnew,"from",jid_full(jid_user(m->s->id)));
			xmlnode_put_attrib(pnew, "from",
					   jid_full(m->s->uid));
			js_session_from(m->s, jpacket_new(pnew));
		}

		if ((cur->subscription == SUBS_FROM
		     || cur->subscription == SUBS_BOTH)
		    && (notify != NULL)) {
			log_debug("we need to notify them");
			jid_append(notify, id);
		}
	}
}

/* check if user roster item is in specified group
   return true if item is in this group */
int js_roster_item_group(udata u, jid id, char *group)
{
	js_roster cur;
	js_roster_group cur_gr;
	if (u == NULL || id == NULL)
		return 0;

	if (load_cache_roster(u) == 0)
		return 0;

	for (cur = (js_roster) u->roster_cache; cur; cur = cur->next) {
		/* user */
		switch (cur->type) {
		case TYPE_USER:
			if (!id->user)
				continue;
			if (strcasecmp(cur->user, id->user) != 0)
				continue;
			if (strcmp(cur->server, id->server) != 0)
				continue;
			break;
		case TYPE_SERVER_RESOURCE:
			if (id->user)
				continue;
			if (!id->resource)
				continue;
			if (strcmp(cur->server, id->server) != 0)
				continue;
			/* resource */
			if (strcmp(cur->user, id->resource) != 0)
				continue;
			break;
		case TYPE_SERVER:
			if (id->user)
				continue;
			if (id->resource)
				continue;
			if (strcmp(cur->server, id->server) != 0)
				continue;
			break;
		case TYPE_USER_RESOURCE:
			if (!id->user)
				continue;
			if (!id->resource)
				continue;
			continue;
			/* XXX */
			break;
		}

		/* founded, check groups */
		if (cur->gr == NULL) {
			/* if user has no groups */
			if (group[0] == '\0')
				return 1;
		} else {
			for (cur_gr = cur->gr; cur_gr;
			     cur_gr = cur_gr->next) {
				if (strcmp(cur_gr->name, group) == 0)
					return 1;
			}
		}

	}

	return 0;
}
