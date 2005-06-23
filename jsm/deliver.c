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

/*
   Modifed by £ukasz Karwacki <lukasm@wp-sa.pl>

changes
 1. remove main hash with hosts
 2. add sem for users and main hash

*/


/* takes any packet and attempts to deliver it to the correct session/thread */
/* must have a valid to/from address already before getting here */
/* deliver threads or session threads */
static void js_deliver_local(jsmi si, jpacket p){
  udata user;
  session s;

  log_debug("delivering locally to %s",jid_full(p->to));

  /* this is for the server */
  if(p->to->user == NULL){
	/* let some modules fight over it */
	if(js_mapi_call(si, e_DELIVER, p, NULL, NULL)){
	  return;
	}

	js_server_main(si,p);
	return;
  }

  user = js_user(si, p->to, 0);
  if (p->to->resource)
	s = js_session_get(user, p->to->resource);
  else
	s = NULL;

  log_debug("delivering locally to %s",jid_full(p->to));

  /* let some modules fight over it */
  if(js_mapi_call(si, e_DELIVER, p, user, s)){
	if (user != NULL){
	  THREAD_DEC(user->ref);
	}
	return;
  }

  if(s != NULL){ /* it's sent right to the resource */
	THREAD_DEC(user->ref);
	js_session_to(s, p);
	return;
  }

  if(user != NULL){ /* valid user, but no session */
	  p->aux1 = (void *)user; /* performance hack, we already know the user */

	  /* if no resource */
	  if (p->type == JPACKET_MESSAGE)
		if (user->sessions){
		  s = js_session_primary(user);
		  if (s){
			THREAD_DEC(user->ref);
			js_session_to(s, p);
			return;
		  }
		}

	  js_offline_main(si,p);
	  return;
  }

  /* no user, so bounce the packet */
  js_bounce(si,p->x,TERROR_NOTFOUND);
}

/* always deliver threads */
static void js_outgoing_packet(jsmi si, jpacket jp, dpacket p){
  session s = NULL;
  udata u;

  /* attempt to locate the session by matching the special resource */
  u = js_user(si, p->id , 0);
  if(u == NULL){
	log_notice(p->host,
			   "Bouncing packet intended for nonexistant user: %s",
			   xmlnode2str(p->x));
	deliver_fail(dpacket_new(p->x), "Invalid User");
	return;
  }

  SEM_LOCK(u->sem);
  for(s = u->sessions; s != NULL; s = s->next)
	if(j_strcmp(p->id->resource, s->route->resource) == 0)
	  break;
  SEM_UNLOCK(u->sem);

  THREAD_DEC(u->ref);

  if(s != NULL){   /* just pass to the session normally */
	js_session_from(s, jp);
	return;
  }

  log_notice(p->host,"Bouncing %s packet intended for session %s",
	     xmlnode_get_name(jp->x),
	     jid_full(p->id));
  deliver_fail(dpacket_new(p->x), "Invalid Session");
}

/* always deliver threads */
inline static void js_session_new_mtq(jsmi si, dpacket p){
  session s;

  if((s = js_session_new(si, p)) == NULL){
	/* session start failed */
	log_warn(p->host,"Unable to create session %s",jid_full(p->id));
	xmlnode_put_attrib(p->x,"type","error");
	xmlnode_put_attrib(p->x,"error","Session Failed");
  }else{
	/* reset to the routed id for this session for the reply below */
	xmlnode_put_attrib(p->x,"to",jid_full(s->route));
  }

  /* reply */
  jutil_tofrom(p->x);
  deliver(dpacket_new(p->x), si->i);
}

/* deliver threads, when offline user or server */
void js_deliver_thread_hander(void *arg, void *value){
  jsmi si = (jsmi)arg;
  packet_thread_p pt = (packet_thread_p)value;

  /* */
  if ((pt->jp)&&(pt->p)){
    log_debug("Get user Simple packet");
    js_outgoing_packet(si,pt->jp,pt->p);
    return;
  }

  if (!pt->p){
    log_debug("SImple packet");
    js_deliver_local(si, pt->jp);
    return;
  }


  log_alert("fatal","error - unknown state");
}

/* any thread */
result js_packet(instance i, dpacket p, void *arg){
    jsmi si = (jsmi)arg;
    jpacket jp = NULL;
    session s = NULL;
    packet_thread_p pt;
    udata u;

    char *type, *authto;

    log_debug("incoming packet %s",xmlnode2str(p->x));

    /* if this is a routed packet */
    if(p->type == p_ROUTE){
	type = xmlnode_get_attrib(p->x,"type");

	/* new session requests */
	if(j_strcmp(type,"session") == 0){
	  js_session_new_mtq(si,p);
	  return r_DONE;
	}

	/* get the internal jpacket */
	if(xmlnode_get_firstchild(p->x) != NULL) /* XXX old libjabber jpacket_new() wasn't null safe, this is just safety */
	    jp = jpacket_new(xmlnode_get_firstchild(p->x));


	/* auth/reg requests */
	if(jp != NULL && j_strcmp(type,"auth") == 0){
	    /* check and see if we're configured to forward auth packets for processing elsewhere */
	    if((authto = xmlnode_get_data(js_config(si,"auth"))) != NULL){
		xmlnode_put_attrib(p->x,"oto",xmlnode_get_attrib(p->x,"to")); /* preserve original to */
		xmlnode_put_attrib(p->x,"to",authto);
		deliver(dpacket_new(p->x), i);
		return r_DONE;
	    }

	    /* internally, hide the route to/from addresses on the authreg request */
	    xmlnode_put_attrib(jp->x,"to",xmlnode_get_attrib(p->x,"to"));
	    xmlnode_put_attrib(jp->x,"from",xmlnode_get_attrib(p->x,"from"));
	    xmlnode_put_attrib(jp->x,"route",xmlnode_get_attrib(p->x,"type"));
	    jpacket_reset(jp);
	    jp->aux1 = (void *)si;

	    pt = pmalloco(jp->p,sizeof(packet_thread_t));
	    pt->jp = jp;
	    fast_mtq_send(si->mtq_authreg, pt);
	    return r_DONE;
	}

	/* if it's an error */
	if(j_strcmp(type,"error") == 0){

		  /* look for users online */
		  u = js_user(si, p->id , 1);
		  if(u == NULL){
			/* if this was a message,
			   it should have been delievered to that session, store offline */
			if(jp != NULL && jp->type == JPACKET_MESSAGE){
			  pt = pmalloco(jp->p, sizeof(packet_thread_t));
			  pt->jp = jp;
			  fast_mtq_send(si->mtq_deliver, pt);
			  return r_DONE;
			}

			xmlnode_free(p->x);
			return r_DONE;
			/*
					log_notice(p->host,
					"Bouncing packet intended for nonexistant user: %s",
					xmlnode2str(p->x));
					deliver_fail(dpacket_new(p->x), "Invalid User");
					return r_DONE;
			*/
		  }

		  if(p->id->resource != NULL){
		    SEM_LOCK(u->sem);
		    for(s = u->sessions; s != NULL; s = s->next)
		      if(j_strcmp(p->id->resource, s->route->resource) == 0)
			break;
		    SEM_UNLOCK(u->sem);

		    if(s != NULL){
		      s->sid = NULL; /* they generated the error,
					no use in sending there anymore! */
		      js_session_end(s, "Disconnected");
		    }
		  }
		  else{
			session temp;
			/* a way to boot an entire user off */
			SEM_LOCK(u->sem);
			for(s = u->sessions; s != NULL;){
			  temp = s;
			  s = s->next;
			  js_session_end_nosem(temp,"Removed");
			}
			u->pass = NULL; /* so they can't log back in */

			SEM_UNLOCK(u->sem);

			xmlnode_free(p->x);
			THREAD_DEC(u->ref);
			return r_DONE;
		  }

		  THREAD_DEC(u->ref);

		  /* if this was a message,
			 it should have been delievered to that session, store offline */
		  if(jp != NULL && jp->type == JPACKET_MESSAGE){
			  pt = pmalloco(jp->p, sizeof(packet_thread_t));
			  pt->jp = jp;
			  fast_mtq_send(si->mtq_deliver, pt);
			  return r_DONE;
		  }

		  /* drop and return */
		  if(xmlnode_get_firstchild(p->x) != NULL)
			log_notice(p->host, "Dropping a bounced session packet to %s",
					   jid_full(p->id));

		  xmlnode_free(p->x);
		  return r_DONE;
		}

		/* uhh, empty packet, *shrug* */
	if(jp == NULL){
	  log_notice(p->host,"Dropping an invalid or empty route packet: %s",xmlnode2str(p->x),jid_full(p->id));
	  xmlnode_free(p->x);
	  return r_DONE;
	}

	/* ============================================================== */


	/* attempt to locate the session by matching the special resource */
	u = js_user(si, p->id , 1);
	if(u == NULL){
	  /* only when route packet to users not online */
	  pt = pmalloco(p->p, sizeof(packet_thread_t));
	  pt->p  = p;
	  pt->jp = jp;
	  fast_mtq_send(si->mtq_deliver, pt);
	  return r_DONE;
	}

	/* user is online :) */
	SEM_LOCK(u->sem);
	for(s = u->sessions; s != NULL; s = s->next)
	  if(j_strcmp(p->id->resource, s->route->resource) == 0)
	    break;
	SEM_UNLOCK(u->sem);

	THREAD_DEC(u->ref);

	if(s != NULL){	 /* just pass to the session normally */
	  js_session_from(s, jp);
	  return r_DONE;
	}

	log_notice(p->host,"Bouncing %s packet intended for session %s",
		   xmlnode_get_name(jp->x),
		   jid_full(p->id));
	deliver_fail(dpacket_new(p->x), "Invalid Session");
	return r_DONE;
    }

    /* normal server-server packet, should we make sure it's not spoofing us?  if so, if ghash_get(p->to->server) then bounce w/ security error */

    jp = jpacket_new(p->x);
    if(jp == NULL){
	log_warn(p->host,"Dropping invalid incoming packet: %s",xmlnode2str(p->x));
	xmlnode_free(p->x);
	return r_DONE;
    }

    pt = pmalloco(jp->p, sizeof(packet_thread_t));
    pt->jp = jp;
    fast_mtq_send(si->mtq_deliver, pt);
    return r_DONE;
}


/* NOTE: any jpacket sent to deliver *MUST* match jpacket_new(p->x),
 * jpacket is simply a convenience wrapper
 */
void js_deliver(jsmi si, jpacket p){

    if(p->to == NULL){
	log_warn(NULL,"jsm: Invalid Recipient, returning data %s",xmlnode2str(p->x));
	js_bounce(si,p->x,TERROR_BAD);
	return;
    }

    if(p->from == NULL){
	log_warn(NULL,"jsm: Invalid Sender, discarding data %s",xmlnode2str(p->x));
	xmlnode_free(p->x);
	return;
    }

    log_debug("deliver(to[%s],from[%s],type[%d],packet[%s])",jid_full(p->to),jid_full(p->from),p->type,xmlnode2str(p->x));

    /* is it our */
    if (j_strcmp(si->host,p->to->server) == 0){
      js_deliver_local(si, p);
      return;
    }

    deliver(dpacket_new(p->x), si->i);
}

