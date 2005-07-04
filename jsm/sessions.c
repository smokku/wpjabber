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
 * sessions.c -- handle messages to and from user sessions
 *
 * --------------------------------------------------------------------------*/

/*
   Modifed by £ukasz Karwacki <lukasm@wp-sa.pl>

changes
1. added js_session_end_nosem
2. added sems

*/

#include "jsm.h"

static void _js_session_start(session s);
static void _js_session_to(void *arg);
static void _js_session_from(void *arg);
static void _js_session_end(session s);

static jsm_mtq_queue_p get_mtq_queue(jsmi si, jsm_mtq_queue_p current){
  jsm_mtq_queue_p q;
  int i;
  int count;

  if (current){
    current->count++;
    return current;
  }

  /* find queue with less users */
  count=999999;
  q = NULL;
  for(i = 0; i < si->mtq_session_count; i++)
    if(si->mtq_session[i]->count < count){
      count = si->mtq_session[i]->count;
      q = si->mtq_session[i];
    }

  q->count++;
  return q;
}

void js_session_thread_hander(void *arg, void *value){
  packet_thread_p pt = (packet_thread_p)value;

  switch (pt->type){
    /* from */
  case 2:
    _js_session_from(pt->jp);
    break;
    /* to */
  case 3:
    _js_session_to(pt->jp);
    break;
    /* start */
  case 0:
    _js_session_start(pt->s);
    free(pt);
    break;
    /* end */
  case 1:
    _js_session_end(pt->s);
    break;
    /* wponline */
  case 4:
    js_post_out(pt->jp);
    break;
  }
}

/* delivers a route packet to all listeners for this session */
static void js_session_route(session s, xmlnode in){
    /* NULL means this is an error from the session ending */
    if(in == NULL){
	 in = xmlnode_new_tag("route");
	 xmlnode_put_attrib(in, "type", "error");
	 xmlnode_put_attrib(in, "error", "Disconnected");
    }else{
	in = xmlnode_wrap(in,"route");
    }

    xmlnode_put_attrib(in, "from", jid_full(s->route));
    xmlnode_put_attrib(in, "to", jid_full(s->sid));
    deliver(dpacket_new(in), s->si->i);
}

/*
 *  js_session_new -- creates a new session, registers the resource for it
 *
 *  Sets up all the data associated with the new session, then send it a
 *  start spacket, which basically notifies all modules about the new session
 *
 *  returns
 *	a pointer to the new session
 */
session js_session_new(jsmi si, dpacket dp){
    pool p;
    session s, cur;
    udata u;
    int i;
    char routeres[10];

    /* screen out illegal calls */
    if(dp == NULL || dp->id->user == NULL || dp->id->resource == NULL || xmlnode_get_attrib(dp->x,"from") == NULL)
	return NULL;

	if ((u = js_user(si,dp->id,0)) == NULL)
	  return NULL;

    if (u->scount >= MAX_USER_SESSIONS){
      THREAD_DEC(u->ref);
      return NULL;
    }


    log_debug("session_create %s",jid_full(dp->id));

    /* create session */
    p = pool_heap(1024);
    s = pmalloco(p, sizeof(struct session_struct));
    s->p = p;
    s->si = si;

    /* save authorative remote session id */
    s->sid = jid_new(p, xmlnode_get_attrib(dp->x,"from"));

    /* session identity */
    s->id = jid_new(p, jid_full(dp->id));
    /* id bez resource */
    s->uid = jid_user(s->id);
    s->route = jid_new(p, jid_full(dp->id));
    snprintf(routeres,9,"%X",s);
    jid_set(s->route, routeres, JID_RESOURCE);
    s->res = pstrdup(p, dp->id->resource);
    s->u = u;

    jid_full(s->sid);
    jid_full(s->uid);
    jid_full(s->id);
    jid_full(s->route);

    {
      register char * text;
      text = xmlnode_get_attrib(dp->x,"ip");
      if (text){
	s->ip = pstrdup(s->p,xmlnode_get_attrib(dp->x,"ip"));
	xmlnode_hide_attrib(dp->x,"ip");
      }
      else
	s->ip = "none";
    }

    /* default settings */
    s->exit_flag = 0;
    s->roster = 0;
    s->priority = -129;
    s->presence = jutil_presnew(JPACKET__UNAVAILABLE,NULL,NULL);
    xmlnode_put_attrib(s->presence,"from",jid_full(s->id));
    s->c_in = s->c_out = 0;

    for(i = 0; i < es_LAST; i++)
      s->events[i] = NULL;

    SEM_LOCK(u->sem);

    if (u->sessions != NULL){
      s->q = get_mtq_queue(si, u->sessions->q);

      for(cur = u->sessions; cur != NULL; cur = cur->next)
	if(j_strcmp(dp->id->resource, cur->res) == 0)
	  js_session_end_nosem(cur, "Replaced by new connection");
    }
    else{
      s->q = get_mtq_queue(si, NULL);
    }

    /* make sure we're linked with the user */
    s->next = s->u->sessions;
    s->u->sessions = s;
    s->u->scount++;

    SEM_UNLOCK(u->sem);

    /* session start */
    {
      packet_thread_p pt;
      pt = malloc(sizeof(packet_thread_t));
      pt->s    = s;
      pt->type = 0;
      fast_mtq_send(s->q->mtq, pt);
    }

    THREAD_DEC(u->ref);
    return s;
}


/*
 *  js_session_end_nosem -- shut down the session without sem locking
 *  parameters
 *	s -- the session to end
 *	reason -- the reason the session is shutting down (for logging)
 *
 */
void js_session_end_nosem(session s, char *reason){
    session cur;    /* used to iterate over the user's session list
		       when removing the session from the list */

    /* ignore illegal calls */
    if(s == NULL || s->exit_flag == 1 || reason == NULL)
	return;

    /* so it doesn't get freed */
    THREAD_INC(s->u->ref);

    /* log the reason the session ended */
    log_debug("end %d '%s'",s,reason);

    /* flag the session to exit ASAP */
    s->exit_flag = 1;

    /* make sure we're not the primary session */
    s->priority = -129;

    /* presence will be updated in _js_session_end to be shore that
       only session thread is changing this value  */

    /*
     * remove this session from the user's session list --
     * first check if this session is at the head of the list
     */
    if(s == s->u->sessions){
	/* yup, just bump up the next session */
	s->u->sessions = s->next;

    }else{

	/* no, we have to traverse the list to find it */
	for(cur = s->u->sessions; cur->next != s; cur = cur->next);
	cur->next = s->next;
    }

    s->u->scount--;

    /* tell it to exit */
    {
      packet_thread_p pt;
      pt = pmalloco(s->p, sizeof(packet_thread_t));
      pt->s    = s;
      pt->type = 1;
      fast_mtq_send(s->q->mtq, pt);
    }
}

/*
 *  js_session_end -- shut down the session
 *
 *  This function gets called when the user disconnects or when the server shuts
 *  down. It changes the user's presence to offline, cleans up the session data
 *  and sends an end spacket
 *
 *  parameters
 *	s -- the session to end
 *	reason -- the reason the session is shutting down (for logging)
 *
 */
void js_session_end(session s, char *reason){
    session cur;    /* used to iterate over the user's session list
		       when removing the session from the list */

    /* ignore illegal calls */
    if(s == NULL || s->exit_flag == 1 || reason == NULL)
	return;

    /* so it doesn't get freed */
    THREAD_INC(s->u->ref);

    /* log the reason the session ended */
    log_debug("end %d '%s'",s,reason);

    /* flag the session to exit ASAP */
    s->exit_flag = 1;

    /* make sure we're not the primary session */
    s->priority = -129;

    /* presence will be updated in _js_session_end to be shore that
       only session thread is changing this value  */

    /*
     * remove this session from the user's session list --
     * first check if this session is at the head of the list
     */
    SEM_LOCK(s->u->sem);
    if(s == s->u->sessions){
	/* yup, just bump up the next session */
	s->u->sessions = s->next;

    }else{

	/* no, we have to traverse the list to find it */
	for(cur = s->u->sessions; cur->next != s; cur = cur->next);
	cur->next = s->next;

    }

    s->u->scount--;
    SEM_UNLOCK(s->u->sem);

    /* end */
    {
      packet_thread_p pt;
      pt = pmalloco(s->p, sizeof(packet_thread_t));
      pt->s    = s;
      pt->type = 1;
      fast_mtq_send(s->q->mtq, pt);
    }
}

/* child that starts a session */
void _js_session_start(session s){
    /* let the modules go to it */
    js_mapi_call(s->si, e_SESSION, NULL, s->u, s);

    /* log the start time of the session */
    s->started = time(NULL);
}

/* child that handles packets from the user */
void _js_session_from(void *arg){
    jpacket p = (jpacket)arg;
    session s = (session)(p->aux1);

    /* if this session is dead */
    if(s->exit_flag){
	/* send the packet into oblivion */
	xmlnode_free(p->x);
	return;
    }

    /* at least we must have a valid packet */
    if(p->type == JPACKET_UNKNOWN){
	/* send an error back */
	jutil_error(p->x,TERROR_BAD);
	jpacket_reset(p);
	js_session_to(s, p);
	return;
    }

    /* debug message */
    log_debug("THREAD:SESSION:FROM received a packet!");

    /* increment packet out count */
    s->si->stats->packets_out++;
    s->c_out++;

    /* make sure we have our from set correctly for outgoing packets */
    if(jid_cmpx(p->from,s->id,JID_USER|JID_SERVER) != 0){
	/* nope, fix it */
	xmlnode_put_attrib(p->x,"from",jid_full(s->id));
	p->from = jid_new(p->p,jid_full(s->id));
    }

    /* if you use to="yourself@yourhost" it's the same as not having a to, the modules use the NULL as a self-flag */
    if(jid_cmp(p->to,s->uid) == 0){
	/* xmlnode_hide_attrib(p->x,"to"); */
	p->to = NULL;
    }

    /* let the modules have their heyday */
    if(js_mapi_call(NULL, es_OUT,  p, s->u, s))
	return;

    /* no module handled it, so restore the to attrib to us */
    if(p->to == NULL){
	xmlnode_put_attrib(p->x,"to",jid_full(s->uid));
	p->to = jid_new(p->p,jid_full(s->uid));
    }

    js_post_out_main(s,p);

    /* pass these to the general delivery function */
	  //	js_deliver(s->si, p);
}

/* child that handles packets to the user */
void _js_session_to(void *arg){
    jpacket p = (jpacket)arg;
    session s = (session)(p->aux1);

    /* if this session is dead... */
    if(s->exit_flag){
	/* ... and the packet is a message */
	if(p->type == JPACKET_MESSAGE)
	    js_deliver(s->si, p);
	else /* otherwise send it to oblivion */
	    xmlnode_free(p->x);
	return;
    }

    /* debug message */
    log_debug("THREAD:SESSION:TO received data from %s!",jid_full(p->from));

    /* increment packet in count */
    s->si->stats->packets_in++;
    s->c_in++;

    /* let the modules have their heyday */
    if(js_mapi_call(NULL, es_IN, p, s->u, s))
	return;

    /* we need to check again, s->exit_flag *could* have changed within the modules at some point */
    if(s->exit_flag){
	/* deliver that packet if it was a message, and sk'daddle */
	if(p->type == JPACKET_MESSAGE)
	    js_deliver(s->si, p);
	else
	    xmlnode_free(p->x);
	return;
    }

    /* deliver to listeners on session */
    js_session_route(s, p->x);
}

result js_session_free(void * arg){
  session s = (session)arg;

  pool_free(s->p);
  return r_UNREG;
}

/* child that cleans up a session
   session thread */
void _js_session_end(session s){
    xmlnode x,temp;

    /* debug message */
    log_debug("THREAD:SESSION exiting");

    /* only for moules es_END */
    temp = s->presence;
    if(temp != NULL && j_strcmp(xmlnode_get_attrib(temp, "type"), "unavailable") != 0){
      /* create a new presence packet with the reason the user is unavailable */
      x = jutil_presnew(JPACKET__UNAVAILABLE,NULL,NULL);
      xmlnode_put_attrib(x,"from",jid_full(s->id));

      /* free the old presence packet */
      s->presence = x;
    }
    else{
      temp = NULL;
    }

    /* make sure the service knows the session is gone */
    if(s->sid != NULL)
      js_session_route(s, NULL);

    /* let the modules have their heyday */
    js_mapi_call(NULL, es_END, NULL, s->u, s);

    /* let the user struct go  */
    THREAD_DEC(s->u->ref);

    /* free old presence */
    if (temp)
      xmlnode_free(temp);

    /* free the session's presence state */
    temp = s->presence;
    s->presence = NULL;

    s->q->count--;

    /* free the session's memory pool */
    register_beat(60, js_session_free, s);

    /* free unavailable presence we have set */
    if (temp)
      xmlnode_free(temp);
}

/*
 *  js_session_get -- find the session for a resource
 *
 *  Given a user and a resource, find the corresponding session
 *  if the user is logged in. Otherwise return NULL.
 *
 *  parameters
 *	user -- the user's udata record
 *	res -- the resource to search for
 *
 *  returns
 *	a pointer to the session if the user is logged in
 *	NULL if the user isn't logged in on this resource
 */
session js_session_get(udata user, char *res){
    session cur;    /* session pointer */

    /* screen out illeagal calls */
    if(user == NULL || res == NULL)
	return NULL;

    /* find the session and return it*/
	SEM_LOCK(user->sem);
    for(cur = user->sessions; cur != NULL; cur = cur->next)
	  if((j_strcmp(res, cur->res) == 0)&&(!cur->exit_flag)){
		SEM_UNLOCK(user->sem);
		return cur;
	  }

    /* find any matching resource that is a subset and return it */
    for(cur = user->sessions; cur != NULL; cur = cur->next)
	  if((j_strncmp(res, cur->res, j_strlen(cur->res)) == 0)&&(!cur->exit_flag)){
		SEM_UNLOCK(user->sem);
		return cur;
	  }

	SEM_UNLOCK(user->sem);

    /* if we got this far, there is no session */
    return NULL;

}

/*
 *  js_session_primary -- find the primary session for the user
 *
 *  Scan through a user's sessions to find the session with the
 *  highest priority and return a pointer to it.
 *
 *  parameters
 *	user -- user data for the user in question
 *
 *  returns
 *	a pointer to the primary session if the user is logged in with at least priority 0
 *	NULL if there are no active sessions
 */
session js_session_primary(udata user){
    session cur, top;

    /* ignore illeagal calls, or users with no sessions */
    if(user == NULL || user->sessions == NULL)
	return NULL;

    /* find primary session */
	SEM_LOCK(user->sem);

	if (user->sessions == NULL){
	  SEM_UNLOCK(user->sem);
	  return NULL;
	}

    top = user->sessions;
    for(cur = top; cur != NULL; cur = cur->next)
	if(cur->priority > top->priority)
	    top = cur;

    /* return it if it's active */
	SEM_UNLOCK(user->sem);

    if (top->priority >= 0)
	return top;

    else /* otherwise there's no active session */
	return NULL;

}

/* do not free sem at the end if session was found */
session js_session_primary_sem(udata user){
    session cur, top;

    /* ignore illeagal calls, or users with no sessions */
    if(user == NULL || user->sessions == NULL)
	return NULL;

    /* find primary session */
	SEM_LOCK(user->sem);

	if (user->sessions == NULL){
	  SEM_UNLOCK(user->sem);
	  return NULL;
	}

    top = user->sessions;
    for(cur = top; cur != NULL; cur = cur->next)
	if(cur->priority > top->priority)
	    top = cur;

    /* return it if it's active */
    if (top->priority >= 0)
	return top;

    else{ /* otherwise there's no active session */
	  SEM_UNLOCK(user->sem);
	  return NULL;
	}

}


void js_session_to(session s, jpacket jp){
    packet_thread_p pt;
    /* queue the call to child, hide session in packet */
    jp->aux1 = (void *)s;

    pt = pmalloco(jp->p, sizeof(packet_thread_t));
    pt->jp = jp;
    pt->type = 3;
    fast_mtq_send(s->q->mtq, pt);
}

void js_session_from(session s, jpacket jp){
    packet_thread_p pt;
    /* queue the call to child, hide session in packet */
    jp->aux1 = (void *)s;

    pt = pmalloco(jp->p, sizeof(packet_thread_t));
    pt->jp = jp;
    pt->type = 2;
    fast_mtq_send(s->q->mtq, pt);
}
