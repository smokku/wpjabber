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
 * main.c - entry point for jsm.so
 * --------------------------------------------------------------------------*/
#include "jsm.h"

/*
   Modifed by £ukasz Karwacki <lukasm@wp-sa.pl>
 */

typedef void (*modcall)(jsmi si);

/* stats beat */
result jsm_stats(void *arg){
    jsmi si = (jsmi)arg;
	time_t t;
	struct tm lt;

	/* if day changed set last max day to zero */
    t = time(NULL);
#ifdef WIN32
    lt = *localtime( &t);
#else
	localtime_r( &t , &lt);
#endif

	/* zero stats */
	if (lt.tm_mday != si->stats->last_max_day){
	  si->stats->last_max_day = lt.tm_mday;
	  si->stats->session_max_yesterday = si->stats->session_max_today;
	  si->stats->session_max_today = 0;
	  si->stats->users_registered_today = 0;
	  log_debug("zero max users stats");
	}

	/* keep users online stats */
	if (si->stats->sessioncount > si->stats->session_max_today){
	  si->stats->session_max_today = si->stats->sessioncount;
	}

	//    pool_stat(0);
    return r_DONE;
}

/* not working now :( */
int __jsm_shutdown(void *arg, void *key, void *data){
    udata u = (udata)data;	/* cast the pointer into udata */
    session cur;

    for(cur = u->sessions; cur != NULL; cur = cur->next){
	  js_session_end(cur, "JSM shutdown");
    }
    return 1;
}


/* callback for walking the host hash tree */
int _jsm_shutdown(void *arg, const void *key, void *data){
    HASHTABLE ht = (HASHTABLE)data;

    log_debug("JSM SHUTDOWN: deleting users for host %s",(char*)key);

    wpghash_walk(ht,__jsm_shutdown,NULL);

    return 1;
}

/** shutdown in MTQ thread */
void jsm_real_shutdown(void *arg){
    jsmi si = (jsmi)arg;

	/* XXX do no't close sessions */
    log_debug("JSM SHUTDOWN: Begining shutdown sequence");
	//	_jsm_shutdown(NULL,si->host,si->users);

	/** modules shutdown */
	js_mapi_call(si, e_SHUTDOWN, NULL, NULL, NULL);

	/** free config */
	//	xmlnode_free(si->config);
}

/** main thread */
void jsm_shutdown(void *arg){
    jsmi si = (jsmi)arg;
    int i;
  //	mtq_send(NULL,NULL,jsm_real_shutdown,arg);

    si->shutdown = 1;

    mtq_stop(si->mtq_deliver);
    mtq_stop(si->mtq_authreg);

    /* send session stop to session threads */
    //	  SEM_LOCK(si->sem);
    //	  wpghash_walk(ht,__jsm_shutdown,NULL);
    //	  js_mapi_call(si, e_SHUTDOWN, NULL, NULL, NULL);

    for (i=0; i < si->mtq_session_count; i++){
      mtq_stop(si->mtq_session[i]->mtq);
    }
}

void js_deliver_thread_hander(void *arg, void *value);
void js_authreg_thread_hander(void *arg, void *value);
void js_session_thread_hander(void *arg, void *value);

void jsm_configure(jsmi si){
  char *str;
  int n,i;
  xmlnode cur;

  log_debug("configuring JSM");

  SEM_INIT(si->sem);

  si->users = wpxhash_new(j_atoi(xmlnode_get_tag_data(si->config,"maxusers"),USERS_PRIME));

  si->stats = pmalloco(si->p,sizeof(_jsmi_stats));
  si->stats->started = time(NULL);
  si->stats->stats_file = xmlnode_get_tag_data(si->config,"stats_file");

  for(n=0;n<e_LAST;n++)
	si->events[n] = NULL;

  si->host = xmlnode_get_tag_data(si->config,"host");

  /* threads */
  cur = xmlnode_get_tag(si->config,"threads");
  n = j_atoi(xmlnode_get_tag_data(cur,"deliver"), DEFAULT_DELIVER_THREADS);
  si->mtq_deliver = fast_mtq_init(n, js_deliver_thread_hander, si);

  log_alert("config","Deliver threads: %d",n);

  n = j_atoi(xmlnode_get_tag_data(cur,"authreg"), DEFAULT_AUTHREG_THREADS);
  si->mtq_authreg = fast_mtq_init(n, js_authreg_thread_hander, si);

  log_alert("config","Authreg threads: %d",n);

  n = j_atoi(xmlnode_get_tag_data(cur,"session"), DEFAULT_SESSION_THREADS);

  log_alert("config","Session threads: %d",n);
  si->mtq_session_count = n;
  si->mtq_session = malloc(sizeof(jsm_mtq_queue_p)*n);
  memset(si->mtq_session, 0, sizeof(jsm_mtq_queue_p)*n);
  for (i=0; i < n; i++){
    si->mtq_session[i] = malloc(sizeof(jsm_mtq_queue_t));
    si->mtq_session[i]->mtq = fast_mtq_init(1, js_session_thread_hander, si);
    si->mtq_session[i]->count = 0;
  }

  /* lowercase the hostname, make sure it's valid characters */
  for(str = si->host; *str != '\0'; str++)
	*str = tolower(*str);

  /* initialize globally trusted ids */
  for(cur = xmlnode_get_firstchild(xmlnode_get_tag(si->config,"admin")); cur != NULL; cur = xmlnode_get_nextsibling(cur)){
	  if(xmlnode_get_type(cur) != NTYPE_TAG) continue;

	  if(si->gtrust == NULL)
		si->gtrust = jid_new(si->p, xmlnode_get_data(cur));
	  else
		jid_append(si->gtrust,jid_new(si->p, xmlnode_get_data(cur)));
    }
}

#ifndef WIN32
/*
 *  start js_users_gc
 */
result js_users_gc_start(void *arg){
  jsmi si = (jsmi)arg;

  register_beat(j_atoi(xmlnode_get_tag_data(si->config,"usergc"),60),js_users_gc,(void *)si);

  return r_UNREG;
}

/* module entry point */
void jsm(instance i, xmlnode x){
    jsmi si;
    xmlnode cur;
    modcall module;

    log_debug("jsm initializing for section '%s'",i->id);

	si = pmalloco(i->p, sizeof(_jsmi));
	si->i = i;
	si->p = i->p;
	si->xc = xdb_cache(i); /* getting xdb_* handle and fetching config */
	si->config = xdb_get(si->xc, jid_new(xmlnode_pool(x),"config@-internal"),"jabber:config:jsm");

    /* create and init the jsm instance handle */
    jsm_configure(si);

    /* fire up the modules by scanning the attribs on the xml we received */
    for(cur = xmlnode_get_firstattrib(x); cur != NULL; cur = xmlnode_get_nextsibling(cur)){
	/* avoid multiple personality complex */
	if(j_strcmp(xmlnode_get_name(cur),"jsm") == 0)
	    continue;

	/* vattrib is stored as firstchild on an attrib node */
	if((module = (modcall)xmlnode_get_firstchild(cur)) == NULL)
	    continue;

	/* call this module for this session instance */
	log_debug("jsm: loading module %s",xmlnode_get_name(cur));
	(module)(si);
    }

    register_shutdown(jsm_shutdown,(void *) si);
    register_phandler(i, o_DELIVER, js_packet, (void *)si);
    register_beat(300,js_users_gc_start,(void *)si);
    register_beat(60,jsm_stats,(void *)si);
}

#endif
