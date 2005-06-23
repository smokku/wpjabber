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
/*
<iq>
  <query xmlns='jabber:iq:privacy'>
   <list name=''>
   <item
    type='[jid|group|subscription]'
    value='jid_name or to/from/both/none/trusted or group_name'
	action='[accept|deny]'
	arg='reply message or forward jid'>
      <presence-out>
      <presence-in>
      <message>
      <iq>
   </item>
   <item ...	/>
   <item ...	/>
   </list>
  </query>
</iq>

Limits:
* rules for jid and group deny all packets,
  subscription and global rules do not deny subscription packets

Module can:
* filter incoming packets
* filter outgoing presences through POSTOUT ( presences )
*(future) handle offline probe presences and sends offline status

TODO:

Config in jsm. Just to enable mod_filter.
 <mod_privacy>
 </mod_privacy>

*/

// #undef NODEBUG

#include "jsm.h"

/* FILTER */
/* rules type */
#define MAX_RULES		50
#define NO_FILTERS	(void *)1

#define RULE_GLOBAL		0
#define RULE_SUBSCRIPTION_MATCH 1
#define RULE_JID_MATCH		2
#define RULE_GROUP_MATCH	3

#define DENIED_GLOBAL	    0
#define DENIED_JID	    1

#define ACTION_RULE_ALLOW   0
#define ACTION_RULE_DENIED  1

#define SUBSCRIPTION_NOTTRUSTED  0
#define SUBSCRIPTION_TRUSTED	 1
#define SUBSCRIPTION_NONE	 2 /* XXX future */
#define SUBSCRIPTION_TO		 3
#define SUBSCRIPTION_FROM	 4
#define SUBSCRIPTION_BOTH	 5

#define BLOCK_ALL	   0
#define BLOCK_MESSAGE	   0x01
#define BLOCK_IQ	   0x02
#define BLOCK_PRESENCE_IN  0x04
#define BLOCK_PRESENCE_OUT 0x08

typedef struct filter_rule_struct
{
  unsigned char type;	/* rule type */
  unsigned char action; /* rule action */
  unsigned char filter;
  union {
	struct {
	  char *jid;  /* string arg, jid or group */
	};
	struct {
	  unsigned int subscription; /* int arg */
	};
  };
  struct filter_rule_struct *next;
} _filter_rule, *filter_rule;

typedef struct user_filters_struct
{
  pool p;
  filter_rule filters;
} _user_filters, *user_filters;

/* return 1 if bad match, 0 if oki */
/* a1 from jid, b1 rule jid, user means that from has user */
inline int compare_jid(char *a1,char *b1,int user){
  register char *a,*b;

  a = a1;
  b = b1;

  log_debug("COMPARE %s %s",a,b);

  /* compare user first  */
  if (user){
	while(*a != '\0' && *b != '\0' && *a != '@' && tolower(*a) == *b){
	  a++; b++;
	}
	/* user compare went wrong */
	if(*a != *b) return 1;
  }
  a++;
  b++;
  /* compare server */
  while(*a == *b && *a != '\0' && *b != '\0' && *a != '/'){
	a++;
	b++;
  }

  /* and here we must check several things
	 sitauations:
	 1. a=\ b=0 -> match oki
	 2. a=0 b=0 -> match oki
	 3. a=\ b=\ -> check resource
	 4. bad match
  */

  if ((*a != *b)&&(*b != '\0')&&(*a != '/')) return 1;

  /* if defined jid has resource */
  if ((*b == '/')&&(*a == '/')){
	a++;
	b++;
		  /* compare resource */
	while(*a != '\0' && *b != '\0' && tolower(*a) == tolower(*b)){
	  a++; b++;
	}
	/* compare went wrong */
	if(*a != *b) return 1;
  }

  return 0;
}

inline int filter_subscription(char *subs){
  if (subs == NULL) return SUBSCRIPTION_NOTTRUSTED;
  if (strcmp(subs,"trusted")==0) return SUBSCRIPTION_TRUSTED;
  if (strcmp(subs,"from")==0) return SUBSCRIPTION_TRUSTED;
  if (strcmp(subs,"both")==0) return SUBSCRIPTION_TRUSTED;
  return SUBSCRIPTION_NOTTRUSTED;
  if (strcmp(subs,"none")==0) return SUBSCRIPTION_NONE;
  if (strcmp(subs,"to")==0) return SUBSCRIPTION_TO;
  if (strcmp(subs,"from")==0) return SUBSCRIPTION_FROM;
  return SUBSCRIPTION_NOTTRUSTED;
}

inline int filter_type(char *type){
  if (type == NULL) return RULE_GLOBAL;
  if (type[0]=='j') return RULE_JID_MATCH;
  if (type[0]=='s') return RULE_SUBSCRIPTION_MATCH;
  if ((type[0]=='g')&&(type[1]=='r')) return RULE_GROUP_MATCH;
  return RULE_GLOBAL;
}

inline int filter_action(char *action){
  if (action[0]=='d') return ACTION_RULE_DENIED;
  /* default is allow */
  return ACTION_RULE_ALLOW;
}

/* allocated rule, xmlnode <item> */
inline void ProcessSubFilter(filter_rule cur,xmlnode x){
  cur->filter = 0;
  if (xmlnode_get_tag(x,"message")) cur->filter |= BLOCK_MESSAGE;
  if (xmlnode_get_tag(x,"iq")) cur->filter |= BLOCK_IQ;
  if (xmlnode_get_tag(x,"presence-in")) cur->filter |= BLOCK_PRESENCE_IN;
  if (xmlnode_get_tag(x,"presence-out")) cur->filter |= BLOCK_PRESENCE_OUT;
}

/* x parm is <item>....<item>... */
/* return 0 if oki
	  1 if no filters
	  2 too many filters */
/* session thread */
int SetSessionUserFilters(user_filters filters, xmlnode x){
  xmlnode cur;
  int count;
  filter_rule last_rule;

  if (x == NULL){
	filters->filters = NO_FILTERS;
	if (filters->p){
	  pool_free(filters->p);
	  filters->p = NULL;
	}
	return 1;
  }

  /* free old filters */
  filters->filters = NULL;
  if (filters->p){
	pool_free(filters->p);
	filters->p = NULL;
  }

  count = 0;
  last_rule = NULL;

  for(cur = x; cur != NULL; cur = xmlnode_get_nextsibling(cur)){
	int type,action;
	char *arg;
	filter_rule rule;

	if (xmlnode_get_type(cur) != NTYPE_TAG) continue;

	if (filters->p == NULL)
	  filters->p = pool_heap(256);

	/* check action */
	arg = xmlnode_get_attrib(cur,"action");
	if (arg == NULL) continue;
	action = filter_action(arg);

	type = filter_type(xmlnode_get_attrib(cur,"type"));

	/* check number of filters */
	if (count >= MAX_RULES){
	  filters->filters = NO_FILTERS;
	  pool_free(filters->p);
	  filters->p = NULL;
	  return 2;
	}

	log_debug("found rule action: %d type %d",action,type);

	switch (action){
	  /*--------------------------------------------------------------------------*/
	case ACTION_RULE_ALLOW:
	  /* build rule */
	  rule = pmalloco(filters->p,sizeof(_filter_rule));
	  rule->type = type;
	  if (rule->type == RULE_JID_MATCH){
		jid j = jid_new(xmlnode_pool(x),xmlnode_get_attrib(cur,"value"));
		if (j==NULL) continue;
		rule->jid = pstrdup(filters->p, jid_full(j));
		if (j->user){
		  unsigned char *str;
		  for(str = rule->jid; *str != '@'; str++){
			*str = tolower(*str);
		  }
		}
	  }
	  else
		if (rule->type == RULE_SUBSCRIPTION_MATCH){
		  rule->subscription = filter_subscription(xmlnode_get_attrib(cur,"value"));
		}
		else
		  if (rule->type == RULE_GROUP_MATCH){
			rule->jid = pstrdup(filters->p, xmlnode_get_attrib(cur,"value"));
			if (rule->jid == NULL) continue;
		  }

	  ProcessSubFilter(rule,cur);

	  rule->action = ACTION_RULE_ALLOW;


	  if (last_rule)
		last_rule->next = rule;
	  else
		filters->filters = rule;
	  last_rule = rule;

	  count++;
	  break;
	  /*--------------------------------------------------------------------------*/
	case ACTION_RULE_DENIED:

	  /* build rule */
	  rule = pmalloco(filters->p,sizeof(_filter_rule));
	  rule->type = type;
	  if (rule->type == RULE_JID_MATCH){
		jid j = jid_new(xmlnode_pool(x),xmlnode_get_attrib(cur,"value"));
		if (j==NULL) continue;
		rule->jid = pstrdup(filters->p, jid_full(j));
		if (j->user){
		  unsigned char *str;
		  for(str = rule->jid; *str != '@'; str++){
			*str = tolower(*str);
		  }
		}
	  }
	  else
		if (rule->type == RULE_SUBSCRIPTION_MATCH){
		  rule->subscription = filter_subscription(xmlnode_get_attrib(cur,"value"));
		}
		else
		  if (rule->type == RULE_GROUP_MATCH){
			rule->jid = pstrdup(filters->p, xmlnode_get_attrib(cur,"value"));
			if (rule->jid == NULL) continue;
		  }

	  ProcessSubFilter(rule,cur);

	  rule->action = ACTION_RULE_DENIED;

	  /* add rule to the list */
	  if (last_rule)
		last_rule->next = rule;
	  else
		filters->filters = rule;
	  last_rule = rule;

	  count++;
	  break;
	}
	/*--------------------------------------------------------------------------*/
  }

  if (filters->filters == NULL){
	filters->filters = NO_FILTERS;
	if (filters->p){
	  pool_free(filters->p);
	  filters->p = NULL;
	}
	return 1;
  }

  return 0;
}


/* return 0 if oki
	  1 if no filters
	  2 too many filters */
int CheckUserFilters(xmlnode x){
  xmlnode cur;
  int count;

  if (x == NULL){
	return 1;
  }

  count = 0;

  for(cur = x;cur != NULL;cur = xmlnode_get_nextsibling(cur)){
	int type, action;
	char *arg;

	if (xmlnode_get_type(cur) != NTYPE_TAG) continue;

	/* get action */
	arg = xmlnode_get_attrib(cur,"action");
	if (arg == NULL) continue;
	action = filter_action(arg);

	/* get type */
	type = filter_type(xmlnode_get_attrib(cur,"type"));

	/* check value */
	if (type != RULE_GLOBAL){
	  if (xmlnode_get_attrib(cur,"value") == NULL) continue;
	  if (type == RULE_JID_MATCH){
		if (!jid_full(jid_new(xmlnode_pool(x),xmlnode_get_attrib(cur,"value"))))
		  continue;
	  }
	}

	/* check for max rules */
	if (count >= MAX_RULES){
	  return 2;
	}

	log_debug("CHECK: found rule action: %d type %d",action,type);

	switch (action){
	  /*--------------------------------------------------------------------------*/
	  /*--------------------------------------------------------------------------*/
	  /*--------------------------------------------------------------------------*/
	case ACTION_RULE_ALLOW:
	case ACTION_RULE_DENIED:
	  count++;
	  break;
	}
	/*--------------------------------------------------------------------------*/
  }

  if (count == 0){
	return 1;
  }

  return 0;
}

/* -------------------------------------------------------------------------
   =========================================================================
   *************************************************************************
   =========================================================================
   ------------------------------------------------------------------------- */
/* if denied */
inline mreturn filter_denied(filter_rule filter, user_filters filters, mapi m, int denied_type){

  if (denied_type == DENIED_JID){
	if (m->packet->type == JPACKET_MESSAGE){
	  goto message_packet;
	}
	xmlnode_free(m->packet->x);
	return M_HANDLED;
  }

  /* s10n packets */
  if (m->packet->type == JPACKET_S10N){
	/* if global denied -> pass s10n packets */
	return M_PASS;
  }

  /* free error packets */
  if (jpacket_subtype(m->packet) == JPACKET__ERROR){
	xmlnode_free(m->packet->x);
	return M_HANDLED;
  }

  if (m->packet->type == JPACKET_MESSAGE){
  message_packet:
	{
	  char *id;
	  xmlnode cur;

	  /* message event */
	  id = xmlnode_get_attrib(m->packet->x,"id");
	  if (id){
		for(cur = xmlnode_get_firstchild(m->packet->x);
			cur;
			cur = xmlnode_get_nextsibling(cur)){
		  if(NSCHECK(cur, NS_EVENT)){
			if(xmlnode_get_tag(cur,"id") != NULL)
			  return M_PASS;
			if(xmlnode_get_tag(cur,"offline") != NULL)
			  break;
		  }
		}
	  }
	  else
		cur = NULL;

	  if (cur){
		log_debug("we have offline event");

		if ((filter->filter&BLOCK_PRESENCE_OUT) &&
			(!(filter->filter&BLOCK_MESSAGE))){
		  xmlnode_hide(cur);

		  log_debug("block for presence out");

		  cur = util_offline_event(NULL,
								   jid_full(m->user->id),
								   xmlnode_get_attrib(m->packet->x,"from"),
								   id);

		  js_deliver(m->si, jpacket_new(cur));
		  return M_PASS;
		}

		log_debug("block for messages");

		cur = util_offline_event(m->packet->x,
								 jid_full(m->user->id),
								 xmlnode_get_attrib(m->packet->x,"from"),
								 id);

		js_deliver(m->si, jpacket_new(cur));
		return M_HANDLED;
	  }
	  else{
		if ((filter->filter&BLOCK_PRESENCE_OUT) &&
			(!(filter->filter&BLOCK_MESSAGE)))
		  return M_PASS;

		xmlnode_free(m->packet->x);
		return M_HANDLED;
	  }
	}
  }

  /* any other packet */
  jutil_error(m->packet->x, TERROR_UNAVAIL);
  js_deliver(m->si, jpacket_new(m->packet->x));
  return M_HANDLED;
}


inline mreturn filter_do_action(filter_rule cur,user_filters filters, mapi m, int denied_type){
  switch (cur->action){
  case ACTION_RULE_DENIED:
	return filter_denied(cur,filters,m,denied_type);
  case ACTION_RULE_ALLOW:
	return M_PASS;
  }
  return M_PASS;
}

/* 1 filters loaded, 0 no filters */
inline int load_user_filters(user_filters filters,mapi m){
  log_debug("get user filters");

  if (filters->p == NULL){
	if (filters->filters == NULL){
	  xmlnode xdb_filters;

	  log_debug("load user filters");

	  /* get filters from xdb */
	  xdb_filters = xdb_get(m->si->xc, m->user->id, NS_PRIVACY);
	  if (xdb_filters == NULL){
		filters->filters = NO_FILTERS;
		return 0;
	  }

	  log_debug("found users filters: %s",xmlnode2str(xdb_filters));

	  SetSessionUserFilters(filters,
							xmlnode_get_firstchild(xmlnode_get_tag(xdb_filters,
																   "list")));

	  xmlnode_free(xdb_filters);

	  if (filters->filters == NO_FILTERS){
		return 0;
	  }
	}
	else
	  return 0; /* no pool and filter != NULL -> no filters */
  }
  return 1;
}

/* =================================================================== */
/* Filter Incoming session packets */
mreturn mod_privacy_in(mapi m, void *arg){
  user_filters filters = (user_filters) arg;
  filter_rule cur;

  log_debug("packets in");

  /* ignore incoming presences, it is too much presences to handle them */
  if(m->packet->type == JPACKET_PRESENCE)
	return M_IGNORE;

  /* do not filter messages from our server */
  if (m->packet->from){
	if (m->packet->from->user == NULL)
	  if (strcmp(m->packet->from->server,m->si->host)==0)
		return M_PASS;

	/* do not filter from us  */
	if (jid_cmp(m->packet->from,m->s->id) == 0)
	  return M_PASS;
  }

  /* filter message packets */
  if(m->packet->type == JPACKET_MESSAGE){
	switch(jpacket_subtype(m->packet)){
	  case JPACKET__NONE:
	  case JPACKET__ERROR:
	  case JPACKET__CHAT:
		break;
	  default:
		return M_PASS;
	  }
  }

  /* no filters */
  if (load_user_filters(filters,m) == 0){
	return M_PASS;
  }

  for (cur = filters->filters; cur != NULL; cur = cur->next){
	/* check filter types */
	if (cur->filter){
	  switch (m->packet->type){
	  case JPACKET_MESSAGE:
		/* if filter is not for message packet */
		if ((!cur->filter&BLOCK_PRESENCE_OUT)&&
			(!cur->filter&BLOCK_MESSAGE)) continue;
		break;
	  case JPACKET_IQ:
		/* if filter is not for IQ packet */
		if (!(cur->filter&BLOCK_IQ)) continue;
		break;
	  case JPACKET_S10N:
		//	  case JPACKET_PRESENCE:
		/* XXX if filter is not for PRESENCE packet */
		if (!(cur->filter&BLOCK_PRESENCE_IN)) continue;
		break;
	  default:
		log_debug("unknown packet. pass.");
		return M_PASS;
	  }
	}

	switch (cur->type){
	case RULE_GLOBAL:
	  log_debug("GLOBAL RULE");
	  return filter_do_action(cur,filters,m,DENIED_GLOBAL);
	  break;
	case RULE_SUBSCRIPTION_MATCH:
	  if (m->packet->from){
		int is_trusted;

		is_trusted = js_trust(m->user, m->packet->from);


		log_debug("COMPARE SUBSCRIPTION from %d, cur %d",is_trusted,
				  cur->subscription);

		if (is_trusted == cur->subscription){
		  log_debug("SUBSCRIPTION RULE");
		  return filter_do_action(cur,filters,m,DENIED_GLOBAL);
		}
	  }
	  break;
	case RULE_GROUP_MATCH:
	  if (m->packet->from){
		if (js_roster_item_group(m->user, m->packet->from,cur->jid)){
		  log_debug("GROUP RULE");
		  return filter_do_action(cur,filters,m,DENIED_JID);
		}
	  }
	  break;
	case RULE_JID_MATCH:
	  /* we must have from */
	  if (m->packet->from){
		if (compare_jid(jid_full(m->packet->from), cur->jid,
						m->packet->from->user != NULL)) continue;

		/* it is our JID */
		log_debug("JID RULE");
		return filter_do_action(cur,filters,m,DENIED_JID);
	  }
	  break;
	}
  }
  return M_PASS;
}

/* =================================================================== */
/* do not send presence if filter is jid or group */
mreturn mod_privacy_postout(mapi m, void *arg){
  user_filters filters = (user_filters) arg;
  filter_rule cur;

  if (m->packet->type != JPACKET_PRESENCE) return M_IGNORE;

  if(jpacket_subtype(m->packet) == JPACKET__PROBE) return M_PASS;

  if (m->packet->to == NULL)
	return M_PASS;

  log_debug("post out");

  if (load_user_filters(filters,m) == 0){
	return M_PASS;
  }

  for (cur = filters->filters; cur; cur = cur->next){

	if (cur->filter){
	  if (!(cur->filter&BLOCK_PRESENCE_OUT)) continue;
	}

	switch (cur->type){
	case RULE_JID_MATCH:
	  if (compare_jid(jid_full(m->packet->to),cur->jid,
					  m->packet->to->user!=NULL)) continue;
	  log_debug("JID RULE");
	  xmlnode_free(m->packet->x);
	  return M_HANDLED;
	  break;
	case RULE_GROUP_MATCH:
	  if (js_roster_item_group(m->user, m->packet->to,cur->jid)){
		log_debug("GROUP RULE");
		xmlnode_free(m->packet->x);
		return M_HANDLED;
	  }
	  break;
	}
  }
  return M_PASS;
}

/* =================================================================== */
mreturn mod_privacy_iq_out(mapi m, void *arg){
  user_filters filters = (user_filters) arg;
  xmlnode xdb_filters;
  int a;

  if (m->packet->type != JPACKET_IQ) return M_IGNORE;

  if(!NSCHECK(m->packet->iq, NS_PRIVACY) || m->packet->to != NULL)
	return M_PASS;

  /* get */
  switch(jpacket_subtype(m->packet)){
  case JPACKET__SET:
	{
	  xmlnode list;
	  list = xmlnode_get_tag(m->packet->iq,"list");
	  if (list == NULL){
		jutil_iqresult(m->packet->x);
		xmlnode_put_attrib(m->packet->x, "type", "error");
		xmlnode_put_attrib(xmlnode_insert_tag(m->packet->x, "error"), "code", "406");
		xmlnode_insert_cdata(xmlnode_get_tag(m->packet->x, "error"), "No list", -1);
		xmlnode_hide(m->packet->iq);
		jpacket_reset(m->packet);
		js_session_to(m->s, m->packet);
		return M_HANDLED;
	  }


	  a = CheckUserFilters(xmlnode_get_firstchild(list));

	  log_debug("FILTER SET check : %d",a);

	  switch (a){
	  case 2:
		jutil_iqresult(m->packet->x);
		xmlnode_put_attrib(m->packet->x, "type", "error");
		xmlnode_put_attrib(xmlnode_insert_tag(m->packet->x, "error"), "code", "406");
		xmlnode_insert_cdata(xmlnode_get_tag(m->packet->x, "error"), "Too many filters", -1);
		break;

	  case 1:
		/* set session filters */
		SetSessionUserFilters(filters,NULL);

		/* save */
		xdb_set(m->si->xc, m->user->id, NS_PRIVACY, NULL);
		jutil_iqresult(m->packet->x);
		break;

	  case 0:
		/* set session filters */
		SetSessionUserFilters(filters, xmlnode_get_firstchild(list));

		/* save */
		xdb_set(m->si->xc, m->user->id, NS_PRIVACY, m->packet->iq);
		jutil_iqresult(m->packet->x);
		break;
	  }
	  xmlnode_hide(m->packet->iq);
	  jpacket_reset(m->packet);
	  js_session_to(m->s, m->packet);
	}
	break;
  case JPACKET__GET:
	log_debug("PRIVACY GET");

	xdb_filters = xdb_get(m->si->xc, m->user->id, NS_PRIVACY);
	xmlnode_put_attrib(m->packet->x, "type", "result");
	xmlnode_insert_node(m->packet->iq, xmlnode_get_firstchild(xdb_filters));

	jpacket_reset(m->packet);
	js_session_to(m->s, m->packet);
	xmlnode_free(xdb_filters);
	break;
  default:
	xmlnode_free(m->packet->x);
  }

  return M_HANDLED;
}

/* =================================================================== */
void mod_privacy_cleanup(void *arg){
  user_filters filters = (user_filters) arg;
  if (filters->p)
	pool_free(filters->p);
}

/* sets up the per-session listeners */
mreturn mod_privacy_session(mapi m, void *arg){
  user_filters filters;

  log_debug("session set-up");

  filters = pmalloco(m->s->p,sizeof(_user_filters));

  /* to configure filters */
  js_mapi_session(es_OUT, m->s, mod_privacy_iq_out, (void*)filters);
  /* handle incoming packets */
  js_mapi_session(es_IN, m->s, mod_privacy_in, (void*)filters);
  /* handle session outgoing packets */
  js_mapi_session(es_POSTOUT, m->s, mod_privacy_postout, (void*)filters);

  pool_cleanup(m->s->p, mod_privacy_cleanup, (void*)filters);

  return M_PASS;
}

/* mod filter */
JSM_FUNC void mod_privacy(jsmi si){
  /* read config */
  if (xmlnode_get_tag(si->config, "mod_privacy")){
	log_debug("init");
	js_mapi_register(si,e_SESSION, mod_privacy_session, NULL);
  }
}
