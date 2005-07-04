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
 * --------------------------------------------------------------------------*/
#include "jsm.h"

typedef struct motd_struct
{
    volatile xmlnode x;
    char *stamp;
    time_t set;
    /*	XXX add sem here for safety */
} *motd, _motd;

int _mod_announce_avail(void *arg, void *key, void *data){
    xmlnode msg = (xmlnode)arg;
    udata u = (udata)data;
    session s = js_session_primary(u);

    if(s == NULL) return 1;

    msg = xmlnode_dup(msg);
    xmlnode_put_attrib(msg,"to",jid_full(s->id));
    js_session_to(s,jpacket_new(msg));

    return 1;
}

mreturn mod_announce_avail(jsmi si, jpacket p){
    xmlnode_put_attrib(p->x,"from",p->to->server);

	SEM_LOCK(si->sem);
	wpghash_walk(si->users, _mod_announce_avail, (void *)(p->x));
	SEM_UNLOCK(si->sem);

    xmlnode_free(p->x);
    return M_HANDLED;
}

mreturn mod_announce_motd(jsmi si, jpacket p, motd a){
  xmlnode old_x;

    if(j_strcmp(p->to->resource,"announce/motd/delete") == 0){
		old_x = a->x;
	a->x = NULL;
	xmlnode_free(p->x);

		xmlnode_free(old_x);
	return M_HANDLED;
    }

    /* store new message for all new sessions */
    xmlnode_put_attrib(p->x,"from",p->to->server);
    jutil_delay(p->x,"Announced");
	old_x = a->x;
    a->x = p->x;
    a->set = time(NULL);
    a->stamp = pstrdup(p->p, jutil_timestamp());

    /* tell current sessions if this wasn't an update */
    if(j_strcmp(p->to->resource,"announce/motd/update") != 0){
	  SEM_LOCK(si->sem);
      wpghash_walk(si->users, _mod_announce_avail, (void *)(a->x));
	  SEM_UNLOCK(si->sem);
	}

	xmlnode_free(old_x);
    return M_HANDLED;
}

mreturn mod_announce_dispatch(mapi m, void *arg){
    int admin = 0;

    if(m->packet->type != JPACKET_MESSAGE) return M_IGNORE;
    if(j_strncmp(m->packet->to->resource,"announce/",9) != 0) return M_PASS;

    log_debug("handling announce message from %s",jid_full(m->packet->from));

	/* server event always has user struct if user is local !! */
	//	admin = js_admin(m->user, ADMIN_WRITE);
	admin = js_admin_jid(m->si, jid_user(m->packet->from), ADMIN_WRITE);

    if(admin){
	  if(j_strncmp(m->packet->to->resource,"announce/online",15) == 0) return mod_announce_avail(m->si, m->packet);
	  if(j_strncmp(m->packet->to->resource,"announce/motd",13) == 0) return mod_announce_motd(m->si, m->packet, (motd)arg);
    }

    js_bounce(m->si,m->packet->x,TERROR_NOTALLOWED);
    return M_HANDLED;
}

mreturn mod_announce_sess_avail(mapi m, void *arg){
    motd a = (motd)arg;
    xmlnode last;
    xmlnode msg;
    int lastt;

    if(m->packet->type != JPACKET_PRESENCE) return M_IGNORE;
    if(a->x == NULL) return M_IGNORE;

    /* as soon as we become available */
    if(!js_online(m))
	return M_PASS;

    /* no announces to sessions with negative priority */
    if (m->s->priority < 0)
	return M_PASS;

    /* check the last time we were on to see if we haven't gotten the announcement yet */
    last = xdb_get(m->si->xc, m->user->id, NS_LAST);
    lastt = j_atoi(xmlnode_get_attrib(last,"last"),0);
	xmlnode_free(last);

    if(lastt > 0 && lastt > a->set){ /* if there's a last and it's newer than the announcement, ignore us */
	  return M_IGNORE;
	}

    /* well, we met all the criteria, we should be announced to */
    msg = xmlnode_dup(a->x);
	if (msg){
	  xmlnode_put_attrib(msg,"to",jid_full(m->s->id));
	  js_session_to(m->s,jpacket_new(msg));
	  return M_IGNORE;
	}

	/* ignore after msg was sended */
    return M_PASS;
}

mreturn mod_announce_sess(mapi m, void *arg){
    motd a = (motd)arg;

    if(a->x != NULL)
	js_mapi_session(es_OUT, m->s, mod_announce_sess_avail, arg);

    return M_PASS;
}

JSM_FUNC void mod_announce(jsmi si){
    motd a;

    a = pmalloco(si->p, sizeof(_motd));
    js_mapi_register(si,e_SERVER,mod_announce_dispatch,(void *)a);
    js_mapi_register(si,e_SESSION,mod_announce_sess,(void *)a);
}


