/* --------------------------------------------------------------------------
 *
 *  WP
 *
 * --------------------------------------------------------------------------*/

#include "wpj.h"

extern ios io__data;
extern wpjs wpj;

/* Handle incoming packets from the xstream associated with an MIO object */
void client_process_xml(io m, int state, void* arg, xmlnode x){
    client_c ci = (client_c)arg;
    cqueue q,q2;
    xmlnode h;
    xmlnode rt;
    char *to;
    int a;
	char buf[200];

    log_debug("process XML: m:%X state:%d, arg:%X, x:%X", m, state, arg, x);

    switch(state){
       case IO_NEW:
	     /* find free ci */
	     ci = wpj->last_free_client;
	     if (ci == NULL){
		   /* no more connections */
		   log_alert("error","No more memory connections!");
		   io_close(m);
		   return;
		 }
	     wpj->last_free_client = ci->next;

	     wpj->clients_count++;

	     log_debug("Connection opened Clients %d",wpj->clients_count);

	     m->cb_arg = (void *)ci;

		 snprintf(buf,9,"%8X",time(NULL)+ci->loc);
		 ci->res = pstrdup(m->p,buf);
		 snprintf(buf,199,"%d@%s/%s",ci->loc,wpj->cfg->hostname,ci->res);
		 ci->id = pstrdup(m->p,buf);
		 ci->m = m;
		 ci->m->pollpos = ci->loc; /* m knows ci number in table */
		 ci->auth_id = NULL;
		 ci->state = state_UNKNOWN;
		 ci->q_start = NULL; /* begining of queue */
		 ci->q_end = NULL;   /* end of queue, put here elements */
		 ci->host = NULL;
		 ci->orghost = NULL;
		 ci->sid = NULL;

		 /* for sessions */
		 ci->real_user.username = NULL;
		 ci->real_user.resource = NULL;
		 ci->real_user.presence = NULL;

		 return;

	case IO_XML_ROOT:
	  to = xmlnode_get_attrib(x,"to");
	  ci->orghost = jid_new(ci->m->p,to);

	  h = xstream_header("jabber:client",NULL,jid_full(ci->orghost));

	  ci->sid = pstrdup(ci->m->p,xmlnode_get_attrib(h,"id"));
	  io_write(ci->m, NULL, xstream_header_char(h), -1);
	  xmlnode_free(h);

	  if (j_strcmp(xmlnode_get_attrib(x,"xmlns"),"jabber:client") != 0){
		ci->state = state_CLOSING;
		io_write(ci->m, NULL, "<stream:error>Invalid Namespace</stream:error></stream:stream>", -1);
		io_close(ci->m);
	  }
	  else if (ci->orghost == NULL){
		ci->state = state_CLOSING;
		io_write(ci->m, NULL, "<stream:error>Did not specify a valid to argument</stream:error></stream:stream>", -1);
		io_close(ci->m);

	  }
	  else if(j_strcmp(xmlnode_get_attrib(x,"xmlns:stream"),"http://etherx.jabber.org/streams")!=0){
		ci->state = state_CLOSING;
		io_write(ci->m, NULL, "<stream:error>Invalid Stream Namespace</stream:error></stream:stream>", -1);
		io_close(ci->m);
	  }
	  xmlnode_free(x);
	  return;


	  /* data from client */
	case IO_XML_NODE:
	  rt = xmlnode_wrap(x, "route");

	  /* if refresh, queue every packet */
	  if ( ci->state == state_REFRESHING_SESSION){
	    q2 = pmalloco(xmlnode_pool(x),sizeof(_cqueue));
	    q2->x = x;
	    q2->next = NULL;
	    if (ci->q_end != NULL)
	      ci->q_end->next = q2;
	    else
	      ci->q_start = q2;

	    ci->q_end = q2;
	    return;
	  }

	  if ( ci->state == state_UNKNOWN ){
	    xmlnode q;
	      q = xmlnode_get_tag(x,"query");

	      if (*(xmlnode_get_name(x)) != 'i' ||
		  (NSCHECK(q,NS_AUTH) == 0 && NSCHECK(q,NS_REGISTER) == 0)){
			// Wszystkie xml'e ktore docieraja (i nie sa to xml'e autoryzacji)
			//  kolejkujemy (jeszcze nie jestesmy zautoryzowani)
			q2 = pmalloco(xmlnode_pool(x),sizeof(_cqueue));
			q2->x = x;
			q2->next = NULL;
			if (ci->q_end != NULL)
			  ci->q_end->next = q2;
			else
			  ci->q_start = q2;

			ci->q_end = q2;
			break;
	      } else if (NSCHECK(q,NS_AUTH)){

			xmlnode_put_attrib(rt, "type", "auth");
			if(j_strcmp(xmlnode_get_attrib(x, "type"), "set")==0){
			  register xmlnode tricky;

			  /* check tricky */
			  tricky = xmlnode_get_tag(q,"trickypassword");
			  if (tricky != NULL){
				log_alert("auth","tricky auth hide");
				xmlnode_hide(tricky);
			  }

			  xmlnode_put_attrib(q, "ip", ci->m->ip);

			  log_debug("Auth set packet received");

			  xmlnode_put_attrib(xmlnode_get_tag(q,"digest"),
					     "sid",ci->sid);
			  ci->auth_id = pstrdup(ci->m->p,xmlnode_get_attrib(x,"id"));

			  if (xmlnode_get_tag_data(q,"username") != NULL)
				ci->real_user.username = pstrdup(ci->m->p,xmlnode_get_tag_data(q,"username"));
			  else
				ci->real_user.username = NULL;

			  if (xmlnode_get_tag_data(q,"resource") != NULL)
				ci->real_user.resource = pstrdup(ci->m->p,xmlnode_get_tag_data(q,"resource"));
			  else
				ci->real_user.resource = NULL;

			  if (ci->auth_id == NULL){
				ci->auth_id = pstrdup(ci->m->p,"lukas_auth_ID");
				xmlnode_put_attrib(x,"id","lukas_auth_ID");
			  }

			  jid_set(ci->orghost,
					  xmlnode_get_data(xmlnode_get_tag(xmlnode_get_tag(x,
																	   "query?xmlns=jabber:iq:auth"),
													   "username")),JID_USER);
			  jid_set(ci->orghost,
					  xmlnode_get_data(xmlnode_get_tag(xmlnode_get_tag(x,
																	   "query?xmlns=jabber:iq:auth"),"resource")),
					  JID_RESOURCE);

			  log_debug("Auth set packet managed OK");

			} else if(j_strcmp(xmlnode_get_attrib(x, "type"), "get")==0){
			  jid_set(ci->orghost,xmlnode_get_data(xmlnode_get_tag(xmlnode_get_tag(x,"query?xmlns=jabber:iq:auth"),"username")),JID_USER);
			}

			/* prepare packet and send to server */
			xmlnode_put_attrib(rt,"from",ci->id);
			xmlnode_put_attrib(rt,"to",jid_full(ci->orghost));

			log_debug("Sending auth packets to server");

			a = 0;

			/* Slemy pakiet do serwera */
			if (wpj->si->state == conn_AUTHD){
			  if (wpj->si->m->state == state_ACTIVE){
				io_write(wpj->si->m, rt, NULL,-1);
				a = 1;
			  }
			}

			if (a == 0){
			  // A jak nie byl polaczony to zwracamy error
			  xmlnode_free(rt);
			  log_alert("error","Didn't write to any server");
			  ci->state = state_CLOSING;
			  io_write(ci->m, NULL, "<stream:error>Server temporarily unavailable</stream:error></stream:stream>", -1);
			  io_close(ci->m);
			}
			break;

	      } else if(NSCHECK(q, NS_REGISTER)){
			xmlnode_put_attrib(rt, "type", "auth");
			jid_set(ci->orghost,
					xmlnode_get_data(xmlnode_get_tag(xmlnode_get_tag(x,
																	 "query?xmlns=jabber:iq:register"),"username")),
					JID_USER);

			xmlnode_put_attrib(rt,"from",ci->id);
			xmlnode_put_attrib(rt,"to",jid_full(ci->orghost));

			a = 0;
			if (wpj->cfg->si)
			  if (wpj->cfg->si->state == conn_AUTHD){
				if (wpj->cfg->si->m->state == state_ACTIVE){
				  io_write(wpj->cfg->si->m, rt, NULL, 0);
				  a = 1;
				}
			  }

			if (a == 0){
			  xmlnode_free(rt);
			  log_alert("err","No server to register");
			  ci->state = state_CLOSING;
			  io_write(ci->m, NULL, "<stream:error>Server temporarily unavailable</stream:error></stream:stream>", -1);
			  io_close(ci->m);
			}
			break;
	      }

	      log_alert("err","Program should not be here !!!");
	      xmlnode_free(rt);
	  }
	  else
	    /* error */
	    if ( ci->state == state_AUTHD ){
		  /* deliver after authed */
		  xmlnode_put_attrib(rt,"from",ci->id);
		  xmlnode_put_attrib(rt,"to",jid_full(ci->host));

		  /* try to catch user presnece packets */
		  if (j_strncmp(xmlnode_get_name(x),"presence",8)==0){
			if (xmlnode_get_attrib(x,"to") == NULL){
			  log_debug("Got status presence: %s",xmlnode2str(x));
			  if (ci->real_user.presence)
				xmlnode_free(ci->real_user.presence);
			  ci->real_user.presence = xmlnode_dup(x);
			}
		  }

		  if (wpj->cfg->si->state == conn_AUTHD)
			io_write(wpj->cfg->si->m, rt, NULL, 0);
		  else
			xmlnode_free(rt);
	    }
	    else{
	      /* if other state */
	      xmlnode_free(rt);
	    }

	  break;

	case IO_CLOSED:
	  if (ci == NULL) return;

	  log_debug("Resetting connection");

	  /* if client authed */
	  if (ci->state == state_AUTHD){
		  /* inform server */
		  if (wpj->cfg->si->state == conn_AUTHD){
			h = xmlnode_new_tag("route");
			xmlnode_put_attrib(h, "type", "error");
			xmlnode_put_attrib(h, "from", ci->id);
			xmlnode_put_attrib(h, "to", jid_full(ci->host));
			if (wpj->cfg->si->m->state == state_ACTIVE )
			  io_write(wpj->cfg->si->m, h, NULL, 0);
			else
			  xmlnode_free(h);
		  }

		  if (ci->orghost){
		    log_alert("Session_close","%s [%s]",jid_full(ci->orghost),m->ip);
		  }
		  else{
		    log_alert("Session_close","[%s]",m->ip);
		  }
		}

	  ci->state = state_CLOSING;
	  ci->m = NULL;

	  /* clean up waiting packets */
	  q = ci->q_start;
	  while(q != NULL){
		q2 = q->next;
		xmlnode_free(q->x);
		q = q2;
	  }

	  ci->q_start = NULL;
	  ci->q_end = NULL;
	  //	  ci->si = NULL;     /* Skasowanie sesji - odwiazanie od serwera. */

	  if (ci->real_user.presence){
		xmlnode_free(ci->real_user.presence);
		ci->real_user.presence = NULL;
	  }

	  wpj->clients_count--;

	  /* Add ci to free list */
	  ci->next = NULL;
	  if (wpj->last_free_client == NULL)
		wpj->last_free_client = ci;
	  else
		wpj->first_free_client->next = ci;
	  wpj->first_free_client = ci;
	  return;

	  break;
    }
}
