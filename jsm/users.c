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
 * users.c -- functions for manipulating data for logged in users
 *
 --------------------------------------------------------------------------*/
/*
   Modifed by £ukasz Karwacki <lukasm@wp-sa.pl>

changes
1. js_user always makes ref++, so remember to make ref--;
2. add stats file

*/

#include "jsm.h"

/*
 *  _js_users_del -- call-back for deleting user from the hash table
 *
 *  This function is called periodically by the user data garbage collection
 *  thread. It removes users aren't logged in from the global hashtable.
 *
 *  parameters
 *	arg -- not used
 *		key -- the users key in the hashtable, not used
 *	data -- the user data to check
 *
 *  returns
 *	1
 */
void _js_users_del(wpxht h, char *key, void *val, void *arg){
    jsmi_stats stats = (jsmi_stats)arg;
    udata u = (udata)val;


    if(u->ref > 0 || (u->sessions != NULL)){
      if (u->sessions != NULL){
		stats->sessioncount += u->scount;
		stats->usercount++;
      }
      return;
    }

    wpxhash_zap(h,key);
    pool_free(u->p);

    return;
}


/*
 *  js_users_gc is a heartbeat that
 *  flushes old users from memory.
 *  keeps stats
 */
result js_users_gc(void *arg){
    jsmi si = (jsmi)arg;

    si->stats->usercount = 0;
    si->stats->sessioncount = 0;

    SEM_LOCK(si->sem);
    wpxhash_walk(si->users,_js_users_del,(void *)si->stats);
    SEM_UNLOCK(si->sem);

	log_debug("%d sessions and %d users",
		  si->stats->sessioncount,
		  si->stats->usercount);

	/* write stats to file after users loop */
	if (si->stats->stats_file){
	  FILE *fd;
	  fd = fopen(si->stats->stats_file, "w");
	  if (fd){
		fprintf(fd, "%d\r\n", si->stats->usercount);
		fprintf(fd, "%d", si->stats->sessioncount);
		fclose(fd);
	  }
	  else{
		log_alert("stats_file","Error opening JSM stats file");
	  }
	}

	return r_DONE;
}



/*
 *  js_user -- gets the udata record for a user
 *
 *  js_user attempts to locate the user data record
 *  for the specifed id. First it looks in current list,
 *  if that fails, it looks in xdb and creates new list entry.
 *  If THAT fails, it returns NULL (not a user).
 */
udata js_user(jsmi si, jid id, int online){
    pool p;
    udata cur, newu;
    char *ustr;
    xmlnode x;
    jid uid;

    if(si == NULL || id == NULL || id->user == NULL) return NULL;

    /* check is it our */
	if (j_strcmp(si->host,id->server))
	  return NULL;

    /* copy the id and convert user to lower case */
    uid = jid_new(id->p, jid_full(jid_user(id)));
    for(ustr = uid->user; *ustr != '\0'; ustr++)
	*ustr = tolower(*ustr);

    /* debug message */
    log_debug("js_user(%s)",jid_full(uid));

    /* try to get the user data from the hash table */
	SEM_LOCK(si->sem);
    if((cur = wpxhash_get(si->users,uid->user)) != NULL){
	  THREAD_INC(cur->ref);
	  SEM_UNLOCK(si->sem);
	  return cur;
	}
	SEM_UNLOCK(si->sem);

	if (online){
	  log_debug("user not online");
	  return NULL;
	}

    /* debug message */
    log_debug("js_user not current");

    /* try to get the user auth data from xdb */
    if((x = xdb_get(si->xc, uid, NS_AUTH)) == NULL)
	return NULL;

    /* create a udata struct */
    p = pool_heap(512);
    newu = pmalloco(p, sizeof(_udata));
    newu->p = p;
    newu->si = si;
    newu->user = pstrdup(p, uid->user);
    newu->pass = pstrdup(p, xmlnode_get_data(x));
    newu->id = jid_new(p,jid_full(uid));
    jid_full(newu->id); /* to avoid race conditions */
    SEM_INIT(newu->sem);

    xmlnode_free(x);

	/* I don't like this */
	SEM_LOCK(si->sem);
    cur = wpxhash_get(si->users,uid->user);
    if(cur == NULL){
	  newu->ref = 1;
	  wpxhash_put(si->users,newu->user,newu);
    }
    else{
	  THREAD_INC(cur->ref);
	  SEM_UNLOCK(si->sem);

	  /* free user struct - it's already in hash */
	  pool_free(newu->p);

	  /* return struct from hash */
	  return cur;
    }

    /* got the user, add it to the user list */
	SEM_UNLOCK(si->sem);
    return newu;
}
