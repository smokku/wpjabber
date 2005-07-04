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
   Modifed by £ukasz Karwacki <lukasm@wp-sa.pl>

changes

*/

#include "jsm.h"

/*
 *  js_bounce -- short_desc
 *
 *  Long_description
 *
 *  parameters
 *	x -- the node to bounce
 *	terr - the error code describing the reason for the bounce
 *
 */
void js_bounce(jsmi si, xmlnode x, terror terr){
    /* if the node is a subscription */
    if(j_strcmp(xmlnode_get_name(x),"presence") == 0 && j_strcmp(xmlnode_get_attrib(x,"type"),"subscribe") == 0){
	/* turn the node into a result tag. it's a hack, but it get's the job done */
	jutil_iqresult(x);
	xmlnode_put_attrib(x,"type","unsubscribed");
	xmlnode_insert_cdata(xmlnode_insert_tag(x,"status"),terr.msg,-1);

	/* deliver it back to the client */
	js_deliver(si, jpacket_new(x));
	return;

    }

    /* if it's a presence packet, just drop it */
    if(j_strcmp(xmlnode_get_name(x),"presence") == 0 || j_strcmp(xmlnode_get_attrib(x,"type"),"error") == 0){
	log_debug("dropping %d packet %s",terr.code,xmlnode2str(x));
	xmlnode_free(x);
	return;
    }

    /* if it's neither of these, make an error message an deliver it */
    jutil_error(x, terr);
    js_deliver(si, jpacket_new(x));

}


/*
 *  js_config -- get a configuration node
 *
 *  parameters
 *	si -- session instance
 *	query -- the path through the tag hierarchy of the desired tag
 *		 eg. for the conf file <foo><bar>bar value</bar><baz/><foo>
 *		 use "foo/bar" to retreive the bar node
 *
 *  returns
 *	a pointer to the xmlnode specified in query
 *	or the root config node if query is null
 */
xmlnode js_config(jsmi si, char *query){
    if(query == NULL)
	return si->config;
    else
	return xmlnode_get_tag(si->config, query);
}

/* macro to make sure the jid is a local user */
inline int js_islocal(jsmi si, jid id){
    if(id == NULL || id->user == NULL) return 0;
    if (j_strcmp(si->host,id->server) == 0)
      return 1;  /* is our */
    else
      return 0;  /* is not our */
}

/* validate admin user, which can be non local user */
int js_admin_jid(jsmi si, jid from, int flag){
    xmlnode cur;

    if(!from) return 0;

	log_debug("admin get from");

	for (cur = xmlnode_get_firstchild(js_config(si, "admin"));cur;
		 cur = xmlnode_get_nextsibling(cur)){
	  if (j_strcmp(xmlnode_get_name(cur),"read")==0){
		if (j_strcasecmp(jid_full(from),xmlnode_get_data(cur)) == 0){
		  log_debug("found read");
		  if (flag & ADMIN_READ) return 1;
		}
	  }
	  if (j_strcmp(xmlnode_get_name(cur),"write")==0){
		if (j_strcasecmp(jid_full(from),xmlnode_get_data(cur)) == 0){
		  log_debug("found write");
		  if (flag & (ADMIN_READ | ADMIN_WRITE)) return 1;
		}
	  }
	}
    return 0;
}

/* macro to validate a user as an admin */
int js_admin(udata u, int flag){
    xmlnode cur;

    if (!u) return 0;

	log_debug("admin get");

	for (cur = xmlnode_get_firstchild(js_config(u->si, "admin"));cur;
		 cur = xmlnode_get_nextsibling(cur)){
	  if (j_strcmp(xmlnode_get_name(cur),"read")==0){
		if (j_strcasecmp(jid_full(u->id),xmlnode_get_data(cur)) == 0){
		  if (flag & ADMIN_READ) return 1;
		}
	  }
	  if (j_strcmp(xmlnode_get_name(cur),"write")==0){
		if (j_strcasecmp(jid_full(u->id),xmlnode_get_data(cur)) == 0){
		  if (flag & (ADMIN_READ | ADMIN_WRITE)) return 1;
		}
	  }
	}
    return 0;
}

/* returns true if this mapi call is for the "online" event, sucks, should just rewrite the whole mapi to make things like this better */
int js_online(mapi m){
    if(m == NULL || m->packet == NULL || m->packet->to != NULL || m->s == NULL || m->s->priority >= -128) return 0;

    if(jpacket_subtype(m->packet) == JPACKET__AVAILABLE || jpacket_subtype(m->packet) == JPACKET__INVISIBLE) return 1;

    return 0;
}

xmlnode util_offline_event(xmlnode old, char *from, char *to, char *id){
  xmlnode x, cur;

  if (!old){
	x = xmlnode_new_tag("message");
	xmlnode_put_attrib(x,"to",to);
	xmlnode_put_attrib(x,"from",from);
  }
  else{
	x = old;
	jutil_tofrom(x);
	xmlnode_hide_attrib(x, "id");

	/* erease everything else in the message */
	for(cur = xmlnode_get_firstchild(x);
		cur; cur = xmlnode_get_nextsibling(cur))
	  xmlnode_hide(cur);
  }

  cur = xmlnode_insert_tag(x,"x");
  xmlnode_put_attrib(cur,"xmlns",NS_EVENT);
  xmlnode_insert_tag(cur,"offline");
  xmlnode_insert_cdata(xmlnode_insert_tag (cur, "id"), id, strlen (id));

  return x;
}




