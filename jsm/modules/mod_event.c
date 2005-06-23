/* --------------------------------------------------------------------------

   mod_event

   module handle offline message events for invisible users

   -------------------------------------------------------------------------- */

#include "jsm.h"

mreturn mod_event_in(mapi m, void *arg){
  xmlnode cur;
  char *id;

  if(m->packet->type != JPACKET_MESSAGE) return M_IGNORE;

  /* if user is invisible */
  if ((j_strcmp(xmlnode_get_attrib(m->s->presence,"type"),"unavailable")) ||
	  (m->s->priority < 0)){
	return M_PASS;
  }

  id = xmlnode_get_attrib(m->packet->x,"id");
  if (!id)
	return M_PASS;

  log_debug("user is invisible");

  /* search message event */
  for(cur = xmlnode_get_firstchild(m->packet->x); cur != NULL; cur = xmlnode_get_nextsibling(cur)){
	if(NSCHECK(cur,NS_EVENT)){
	  if(xmlnode_get_tag(cur,"id") != NULL)
		return M_PASS;
	  if(xmlnode_get_tag(cur,"offline") != NULL)
		break;
	}
  }

  if (cur){
	log_debug("offline event");

	/* remove events from original message */
	xmlnode_hide(cur);

	cur = util_offline_event(NULL,
							 jid_full(m->user->id),
							 xmlnode_get_attrib(m->packet->x,"from"),
							 id);

	js_deliver(m->si, jpacket_new(cur));
  }

  return M_PASS;
}

/* sets up the per-session listeners */
mreturn mod_event_session(mapi m, void *arg){
    log_debug("session init");

    js_mapi_session(es_IN, m->s, mod_event_in, NULL);

    return M_PASS;
}

JSM_FUNC void mod_event(jsmi si){
    log_debug("mod_event init");
    js_mapi_register(si, e_SESSION, mod_event_session, NULL);
}
