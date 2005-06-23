#include "jsm.h"

/* mod_offline must go before mod_presence */

#define OFFLINE_ERASE_OTHER_EVENTS 1

typedef struct offlinecount_struct {
  char *host;
  char *ns;
  int deliver;
} *offlinecount_p, offlinecount_t;

static void send_event(mapi m, offlinecount_p off, char *user,
		       int count, char *last){
  xmlnode cur;
  xmlnode x;

  log_debug("Send event %s  : %d", user, count);

  cur = jutil_iqnew(-1,"jabber:iq:offlinecount");
  xmlnode_put_attrib(cur,"from",user);
  xmlnode_put_attrib(cur,"to", off->host);
  x = xmlnode_get_tag(cur,"query");

  if (count > 0){
    char buf[10];
    sprintf(buf,"%d",count);
    xmlnode_insert_cdata(xmlnode_insert_tag(x,"count"),
			 buf,
			 strlen(buf));
  }

  if (last){
    log_debug("Add last %s", last);

    xmlnode_insert_cdata(xmlnode_insert_tag(x,"last"),
			 last,
			 strlen(last));
  }

  deliver(dpacket_new(cur),m->si->i);
}

/* handle an offline message */
mreturn mod_offline_count_message(mapi m, void *arg){
#ifdef OFFLINE_SESSION_CHECK
    session top;
#endif
    offlinecount_p off = (offlinecount_p) arg;
    xmlnode cur = NULL, cur2;
    char str[10];
    char *id;


    /* if there's an existing session, just give it to them */
#ifdef OFFLINE_SESSION_CHECK
    if ((top = js_session_primary_sem(m->user)) != NULL){
	  js_session_to(top, m->packet);
	  return M_HANDLED;
    }
#endif

    switch(jpacket_subtype(m->packet)){
    case JPACKET__NONE:
    case JPACKET__ERROR:
    case JPACKET__CHAT:
	break;
    default:
	return M_PASS;
    }

	/* look for event messages */
	id = xmlnode_get_attrib(m->packet->x,"id");
	if (id){
	  for(cur = xmlnode_get_firstchild(m->packet->x); cur != NULL; cur = xmlnode_get_nextsibling(cur))
		if(NSCHECK(cur,NS_EVENT)){
		  if(xmlnode_get_tag(cur,"id") != NULL)
			return M_PASS; /* bah, we don't want to store events offline (XXX: do we?) */
		  if(xmlnode_get_tag(cur,"offline") != NULL)
			break; /* cur remaining set is the flag */
		}
	  /* erase any other events */
#ifdef OFFLINE_ERASE_OTHER_EVENTS
	  if (cur)
		xmlnode_hide(cur);
#endif
	}


    log_debug("handling message for %s", m->user->user);

    if((cur2 = xmlnode_get_tag(m->packet->x,"x?xmlns=" NS_EXPIRE)) != NULL){

	  /* do not put to offline message with expire < 2 sec */
	  if(j_atoi(xmlnode_get_attrib(cur2, "seconds"),0) < 2)
		  return M_PASS;

	  sprintf(str,"%d",(int)time(NULL));
	  xmlnode_put_attrib(cur2,"stored",str);
    }
    jutil_delay(m->packet->x,"Offline Storage");


    /* try to put */
    if(xdb_act(m->si->xc, m->user->id, NS_OFFLINE, "insert", NULL, m->packet->x)){
      /* free messages from us */
      if(jid_cmpx(m->packet->from, m->user->id, JID_USER | JID_SERVER) == 0){
	log_alert("offline_error","Offline message from own user %s generated offline storage error", m->user->user);
	xmlnode_free(m->packet->x);
	return M_HANDLED;
      }

      /* replay message */
      jutil_tofrom(m->packet->x);

      /* hide everythink */
      for(cur = xmlnode_get_firstchild(m->packet->x); cur != NULL; cur = xmlnode_get_nextsibling(cur))
	xmlnode_hide(cur);

      /* build message */
      {
	char *body;
#ifdef FORWP
	body = "System: Wiadomosci nie dostarczono. Skrzynka wiadomosci jest pelna.";
#else
	body = "System: Message wasn't delivered. Offline storage size was exceeded.";
#endif
	xmlnode_insert_cdata (xmlnode_insert_tag (m->packet->x, "body"), body, -1);
      }
      log_alert("offline_error","Offline message not delivered from user %s",jid_full(m->packet->from));

      js_deliver(m->si, jpacket_reset(m->packet));

      return M_HANDLED;
    }

    /* count ++ */
    if (off->ns){
      char *jid_from;
      int count;
      cur2 = xdb_get(m->si->xc, m->user->id, off->ns);
      if (cur2){
	char buf[10];
	int change = 1;

	jid_from = jid_full(jid_user(m->packet->from));

	count = j_atoi(xmlnode_get_attrib(cur2,"count"), 0);
	if (count > 0){
	  if (j_strcmp( jid_from, xmlnode_get_attrib(cur2,"lastfrom"))==0)
	    change = 0;
	}

	if (change){
	  count++;
	  snprintf(buf,9,"%d",count);

	  if (off->host){
	    send_event(m, off, jid_full(m->user->id), count, jid_from);
	  }

	  xmlnode_put_attrib(cur2, "count", buf);
	  xmlnode_put_attrib(cur2, "lastfrom", jid_from);
	  xdb_set(m->si->xc, m->user->id, off->ns, cur2);

	}
	xmlnode_free(cur2);
      }
      else{
	cur2 = xmlnode_new_tag("messagecount");
	jid_from = jid_full(jid_user(m->packet->from));

	if (off->host){
	  send_event(m, off, jid_full(m->user->id), 1, jid_from);
	}

	xmlnode_put_attrib(cur2, "count", "1");
	xmlnode_put_attrib(cur2, "lastfrom", jid_from);
	xdb_set(m->si->xc, m->user->id, off->ns, cur2);

	xmlnode_free(cur2);
      }
    }

    if (cur != NULL){
      log_debug("offline event");
      cur = util_offline_event(m->packet->x,
			       jid_full(m->user->id),
			       xmlnode_get_attrib(m->packet->x,"from"),
			       id);
      js_deliver(m->si, jpacket_new(cur));
    }
    else{
      xmlnode_free(m->packet->x);
    }

    return M_HANDLED;
}

/* just breaks out to our message/presence offline handlers */
mreturn mod_offline_count_handler(mapi m, void *arg){
    if(m->packet->type == JPACKET_MESSAGE)
      return mod_offline_count_message(m, arg);

    return M_IGNORE;
}

/* watches for when the user is available and sends out offline messages */
void mod_offline_count_out_available(mapi m, void *arg){
    xmlnode opts, cur, x;
    offlinecount_p off = (offlinecount_p) arg;
    int now = time(NULL);
    int expire, stored, diff;
    char str[10];

    log_debug("avability established, check for messages");

    if((opts = xdb_get(m->si->xc, m->user->id, NS_OFFLINE)) == NULL)
	return;

    /* check for msgs */
    for(cur = xmlnode_get_firstchild(opts); cur != NULL; cur = xmlnode_get_nextsibling(cur)){
	/* check for expired stuff */
	if((x = xmlnode_get_tag(cur,"x?xmlns=" NS_EXPIRE)) != NULL){
	    expire = j_atoi(xmlnode_get_attrib(x,"seconds"),0);
	    stored = j_atoi(xmlnode_get_attrib(x,"stored"),now);
	    diff = now - stored;
	    if(diff >= expire){
		log_debug("dropping expired message %s",xmlnode2str(cur));
		xmlnode_hide(cur);
		continue;
	    }
	    sprintf(str,"%d",expire - diff);
	    xmlnode_put_attrib(x,"seconds",str);
	    xmlnode_hide_attrib(x,"stored");
	}
	js_session_to(m->s,jpacket_new(xmlnode_dup(cur)));
	xmlnode_hide(cur);
    }
    /* messages are gone, save the new sun-dried opts container */
    xdb_set(m->si->xc, m->user->id, NS_OFFLINE, NULL);

    if (off->host){
      send_event(m, off, jid_full(m->user->id), 0, NULL);
    }

    if (off->ns){
      xdb_set(m->si->xc, m->user->id, off->ns, NULL);
    }

    xmlnode_free(opts);
}

mreturn mod_offline_count_out(mapi m, void *arg){
    if(m->packet->type != JPACKET_PRESENCE) return M_IGNORE;

    if(js_online(m))
	mod_offline_count_out_available(m, arg);

    return M_PASS;
}

/* sets up the per-session listeners */
mreturn mod_offline_count_session(mapi m, void *arg){
    log_debug("session init");

    js_mapi_session(es_OUT, m->s, mod_offline_count_out, arg);

    return M_PASS;
}

mreturn mod_offline_count_server(mapi m, void *arg){
  offlinecount_p off = (offlinecount_p) arg;
  char * type;

  if(m->packet->type != JPACKET_IQ) return M_IGNORE;

  /* must be to */
  if(m->packet->to == NULL) return M_PASS;

  /* only from our host service  */
  if (j_strcmp(m->packet->from->server, off->host) != 0) return M_PASS;

  /* if not our command */
  if (!NSCHECK(m->packet->iq, "jabber:iq:offlinecount")) return M_PASS;

  /* check type */
  type = xmlnode_get_attrib(m->packet->iq,"type");

  if (j_strcmp(type,"available")==0)
    off->deliver = 1;
  else if (j_strcmp(type,"unavailable")==0)
    off->deliver = 0;

  log_debug("Deliver is %d", off->deliver);

  xmlnode_free(m->packet->x);
  return M_HANDLED;
}

JSM_FUNC void mod_offline_count(jsmi si){
  offlinecount_p off;
  xmlnode temp;
  char *text;

  log_debug("mod_offline_count init");

  off = pmalloco(si->p, sizeof(offlinecount_t));

  /* read config */
  temp = xmlnode_get_tag(si->config, "mod_offline_count");
  if (temp){

    log_debug("Got config %s", xmlnode2str(temp));

     text = xmlnode_get_tag_data(temp, "host");
     if (temp)
	off->host = pstrdup(si->p, text);

     text = xmlnode_get_tag_data(temp, "ns");
     if (temp)
	off->ns = pstrdup(si->p, text);

     if (off->host){
      log_debug("offlinecount to host: %s",off->host);
     }

    if (off->ns){
      log_debug("offlinecount XDB  ns: %s",off->ns);
    }
  }

  js_mapi_register(si, e_SERVER , mod_offline_count_server, off);
  js_mapi_register(si, e_OFFLINE, mod_offline_count_handler, off);
  js_mapi_register(si, e_SESSION, mod_offline_count_session, off);
}
