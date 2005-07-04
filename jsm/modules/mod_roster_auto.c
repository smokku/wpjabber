
#include "jsm.h"

mreturn mod_roster_auto_in_s10n(mapi m, void *arg)
{
	xmlnode reply, x;

	log_debug("AUTO ROSTER");

	//in not s10n
	if (m->packet->type != JPACKET_S10N)
		return M_IGNORE;
	//if no to
	if (m->packet->to == NULL)
		return M_PASS;
	//if from me
	if (jid_cmpx(m->s->uid, m->packet->from, JID_USER | JID_SERVER) ==
	    0)
		return M_PASS;

	log_debug("handling incoming s10n");

	switch (jpacket_subtype(m->packet)) {
	case JPACKET__SUBSCRIBE:
		log_debug("SUBSCRIBE");
		reply =
		    jutil_presnew(JPACKET__SUBSCRIBED,
				  jid_full(m->packet->from), NULL);
		js_session_from(m->s, jpacket_new(reply));
		reply =
		    jutil_presnew(JPACKET__SUBSCRIBE,
				  jid_full(m->packet->from), NULL);
		js_session_from(m->s, jpacket_new(reply));
		break;
	case JPACKET__SUBSCRIBED:
		break;
	case JPACKET__UNSUBSCRIBE:
		log_debug("UNSUBSCRIBE");
		//reply = jutil_presnew(JPACKET__UNSUBSCRIBED, jid_full(m->packet->from),NULL);
		//js_session_from(m->s, jpacket_new(reply));
		//remove account.
		reply = jutil_iqnew(JPACKET__SET, NS_ROSTER);
		x = xmlnode_get_tag(reply, "query");
		x = xmlnode_insert_tag(x, "item");
		xmlnode_put_attrib(x, "jid",
				   jid_full(jid_user(m->packet->from)));
		xmlnode_put_attrib(x, "subscription", "remove");
		js_session_from(m->s, jpacket_new(reply));

		// reply = jutil_iqnewpresnew(JPACKET__UNSUBSCRIBE, jid_full(m->packet->from),NULL);
		// js_session_from(m->s, jpacket_new(reply));
		break;
	case JPACKET__UNSUBSCRIBED:
		break;
	}

	xmlnode_free(m->packet->x);
	return M_HANDLED;
}

mreturn mod_roster_auto_session(mapi m, void *arg)
{
	//incoming packets
	js_mapi_session(es_IN, m->s, mod_roster_auto_in_s10n, NULL);
	return M_PASS;
}


JSM_FUNC void mod_roster_auto(jsmi si)
{
	log_debug("mod auto roster init");
	js_mapi_register(si, e_SESSION, mod_roster_auto_session, NULL);
}
