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

/**
 * @file mod_presence.c
 * @brief handles presences: send to subscribers, send offline on session end, probe for subscribed presences
 *
 * This module is responsible for sending presences to all contacts that subscribed to the user's presence.
 * It will send an unavailable presence to everybudy that got an available presence. It will send
 * probes to the contacts to which we have subscribed presence.
 *
 * This module is NOT responsible for handling subscriptions, they are handled in mod_roster.c.
 *
 * We have three sets of jids for each user:
 * - T: trusted (roster s10ns)
 * - A: availables (who knows were available) - stored in modpres_struct
 * - I: invisibles (who were invisible to) - stored in modpres_struct
 *
 * action points:
 * - broadcasting available presence: intersection of T and A
 *   (don't broadcast updates to them if they don't think we're
 *   available any more, either we told them that or their jidd i
 *   returned a presence error)
 * - broadcasting unavailable presence: union of A and I
 *   (even invisible jids need to be notified when going unavail,
 *   since invisible is still technically an available presence
 *   and may be used as such by a transport or other remote service)
 * - allowed to return presence to a probe when available: compliment
 *   of I in T (all trusted jids, except the ones were invisible to,
 *   may poll our presence any time)
 * - allowed to return presence to a probe when invisible:
 *   intersection of T and A (of the trusted jids, only the ones
 *   we've sent availability to can poll, and we return a generic
 *   available presence)
 * - individual avail presence: forward, add to A, remove from I
 * - individual unavail presence: forward and remove from A, remove from I
 * - individual invisible presence: add to I, remove from A
 * - first avail: populate A with T and broadcast
 */

/**
 * @brief hold configuration data for this module instance
 *
 * This structure holds all information about the configuration of this module.
 */
typedef struct modpres_conf_struct {
	jid bcc;	/**< who gets a blind carbon copy of our presences */
	int pres_to_xdb;/**< if the (primary) presence of a user should be stored in xdb */
} *modpres_conf, _modpres_conf;

/**
 * @brief hold all data belonging to this module and a single (online) user
 *
 * This structure holds the A and I (see description of mod_presence.c) list for a user,
 * a flag if a user is invisible, and a mod_presence config structure
 */
typedef struct modpres_struct {
	int invisible;
	jid A;
	jid I;
	modpres_conf conf;	/**< configuration of this module's instance */
} *modpres, _modpres;


/**
 * util to check if someone knows about us
 *
 * checks if the JID id is contained in the JID list ids
 *
 * @param id the JabberID that should be checked
 * @param ids the list of JabberIDs
 * @return 1 if it is contained, 0 else
 */
int _mod_presence_search(jid id, jid ids)
{
	jid cur;
	for (cur = ids; cur != NULL; cur = cur->next)
		if (jid_cmp(cur, id) == 0)
			return 1;
	return 0;
}

/**
 * remove a jid from a list, returning the new list
 *
 * @param id the JabberID that should be removed
 * @param ids the list of JabberIDs
 * @return the new list
 */
jid _mod_presence_whack(jid id, jid ids)
{
	jid curr;

	if (id == NULL || ids == NULL)
		return NULL;

	/* check first */
	if (jid_cmp(id, ids) == 0)
		return ids->next;

	/* check through the list, stopping at the previous list entry to a matching one */
	for (curr = ids; curr != NULL && jid_cmp(curr->next, id) != 0;
	     curr = curr->next);

	/* clip it out if found */
	if (curr != NULL)
		curr->next = curr->next->next;

	return ids;
}

/**
 * broadcast a presence stanza to a list of JabberIDs
 *
 * this function broadcasts the stanza given as x to all users that are in the notify list of JabberIDs
 * as well as in the intersect list of JabberIDs. If intersect is a NULL pointer the presences are
 * broadcasted to all JabberIDs in the notify list.
 *
 * @param s the session of the user owning the presence
 * @param notify list of JabberIDs that should be notified
 * @param x the presence that should be broadcasted
 * @param intersect if non-NULL only send presence to the intersection of notify and intersect
 */
void _mod_presence_broadcast(session s, jid notify, xmlnode x, jid intersect)
{
	jid cur;
	xmlnode pres;

	for (cur = notify; cur != NULL; cur = cur->next) {
		if (intersect != NULL && !_mod_presence_search(cur, intersect))
			continue;	/* perform insersection search, must be in both */
		s->c_out++;
		pres = xmlnode_dup(x);
		xmlnode_put_attrib(pres, "to", jid_full(cur));

		/* send it to POSTOUT for filter purposes */
		js_post_out_main(s, jpacket_new(pres));
	}
}

void _mod_presence_broadcast_trusted(session s, jid notify, xmlnode x)
{
	jid cur;
	xmlnode pres;

	for (cur = notify; cur != NULL; cur = cur->next) {
		if (!js_istrusted(s->u, cur))
			continue;	/* perform insersection search, must be in both */
		s->c_out++;
		pres = xmlnode_dup(x);
		xmlnode_put_attrib(pres, "to", jid_full(cur));

		/* send it to POSTOUT for filter purposes */
		js_post_out_main(s, jpacket_new(pres));
	}
}

/**
 * filter the incoming presence to this session
 *
 * incoming presence probes get handled and replied if the sender is allowed to see the user's presence and there is a session (presence)
 *
 * filters presences which are sent by the user itself
 *
 * removes JabberIDs from the list of entites that know a user is online if a presence bounced
 *
 * converts incoming invisible presences to unavailable presences as users should not get invisible presences at all
 *
 * @param m the mapi structure
 * @param arg the modpres structure containing the module data belonging to the user's session
 * @return M_IGNORE if stanza is no presence, M_HANDLED if a presence should not be delivered or has been completely handled, M_PASS else
 */
/* session thread */
mreturn mod_presence_in(mapi m, void *arg)
{
	modpres mp = (modpres) arg;
	xmlnode pres;

	if (m->packet->type != JPACKET_PRESENCE)
		return M_IGNORE;

	log_debug("incoming filter for %s", jid_full(m->s->id));

	if (jpacket_subtype(m->packet) == JPACKET__PROBE) {
		/* reply with our presence */
		if (m->s->presence == NULL) {
			log_debug("probe from %s and no presence to return", jid_full(m->packet->from));
		} else if (!mp->invisible && js_trust(m->user, m->packet->from) && !_mod_presence_search(m->packet->from, mp->I)) {
			/* compliment of I in T */
			log_debug("got a probe, responding to %s", jid_full(m->packet->from));
			pres = xmlnode_dup(m->s->presence);
			xmlnode_put_attrib(pres, "to", jid_full(m->packet->from));
			js_session_from(m->s, jpacket_new(pres));
		} else if (mp->invisible && js_trust(m->user, m->packet->from) && _mod_presence_search(m->packet->from, mp->A)) {
			/* when invisible, intersection of A and T */
			log_debug("got a probe when invisible, responding to %s", jid_full(m->packet->from));
			pres = jutil_presnew(JPACKET__AVAILABLE, jid_full(m->packet->from), NULL);
			js_session_from(m->s, jpacket_new(pres));
		} else {
			log_debug("%s attempted to probe by someone not qualified", jid_full(m->packet->from));
		}
		xmlnode_free(m->packet->x);
		return M_HANDLED;
	}

	if (jid_cmp(m->packet->from, m->s->id) == 0) {
		/* this is our presence, don't send to ourselves */
		xmlnode_free(m->packet->x);
		return M_HANDLED;
	}

	/* if a presence packet bounced, remove from the A list */
	if (jpacket_subtype(m->packet) == JPACKET__ERROR)
		mp->A = _mod_presence_whack(m->packet->from, mp->A);

	/* doh! this is a user, they should see invisibles as unavailables */
	if (jpacket_subtype(m->packet) == JPACKET__INVISIBLE)
		xmlnode_put_attrib(m->packet->x, "type", "unavailable");

	return M_PASS;
}

/**
 * process the roster to probe outgoing s10ns, and populate a list of the jids that should be notified
 *
 * this function requests the roster from xdb and does for each contact:
 * - if the user is subscribed to the contacts presence: send a presence probe
 * - if the user has a subscription from the contact: adds the contacts JabberID to the existing list given as parameter \a notify
 *   (if this parameter is NULL, than this function does not care about other users that have subscribed to us)
 *
 * @note the argument given as \a notify is the list A, this list is initialized to contain the user itself, therefore
 * there is always already a first element in the list and we can just append new items. Still I don't like that we do
 * not pass back a pointer to the resulting list, that would allow use to handle an empty initial list as well.
 *
 * @param m the mapi structure
 * @param notify list where contacts that are subscribed to the users presences should be added, if this is NULL we don't add anything
 */
void mod_presence_roster(mapi m, jid notify)
{
	js_trust_populate_presence(m, notify);
}

/**
 * store the top presence information to xdb
 *
 * @param m the mapi struct
 */
void mod_presence_store(mapi m)
{
	/* get the top session */
	session top = js_session_primary(m->user);

	/* store to xdb */
	xdb_set(m->si->xc, m->user->id, NS_JABBERD_STOREDPRESENCE, top ? top->presence : NULL);
}

/**
 * handles undirected outgoing presences (presences with no to attribute)
 *
 * checks that the presence's priority is in the valid range
 *
 * if the outgoing presence is an invisible presence and we are available,
 * we inject an unavailable presence first and reinject the unavailable
 * presence afterwards again (we are then not available anymore and therefore
 * will not do this twice for the same presence)
 *
 * If the outgoing presence is not an invisible presence, it is stored in the structure
 * of this session in the session manager and the presence is stamped with the
 * current timestamp.
 *
 * Unavailable presences are broadcasted to everyone that thinks we are online,
 * available presence are broadcasted to everyone that has subscribed to our presence.
 *
 * If we are already online with other resources, our existing presences are sent to
 * our new resource.
 *
 * Presence probes are sent out to our contacts we are subscribed to.
 *
 * @note this is our second callback for outgoing presences, mod_presence_avails() should have handled the presence first
 *
 * @todo think about if we shouldn't check the presence's priority earlier, maybe in mod_presence_avails()
 *
 * @param m the mapi structure
 * @param arg pointer to the modpres structure containing the module's data for this session
 * @return M_IGNORE if the stanza is no presence, M_PASS if the presence has a to attribute, is a probe, or is an error presence, M_HANDLED else
 */
/* session thread */
mreturn mod_presence_out(mapi m, void *arg)
{
	xmlnode pnew, delay, pold;
	modpres mp = (modpres) arg;
	session cur = NULL;
	int oldpri, newpri;
	char *priority;

	if (m->packet->type != JPACKET_PRESENCE)
		return M_IGNORE;

	if (m->packet->to != NULL
	    || jpacket_subtype(m->packet) == JPACKET__PROBE
	    || jpacket_subtype(m->packet) == JPACKET__ERROR)
		return M_PASS;

	log_debug("new presence from %s of %s", jid_full(m->s->id), xmlnode2str(m->packet->x));

	/* pre-existing conditions (no, we are not an insurance company) */
	oldpri = m->s->priority;

	/* check that the priority is in the valid range */
	priority = xmlnode_get_tag_data(m->packet->x, "priority");
	if (priority == NULL) {
		newpri = 0;
	} else {
		newpri = j_atoi(priority, 0);
		if (newpri < -128 || newpri > 127) {
			log_notice("mod_presence", "got presence with invalid priority value from %s", jid_full(m->s->id));
			xmlnode_free(m->packet->x);
			return M_HANDLED;
		}
	}

	/* invisible mode is special, don't you wish you were special too? */
	if (jpacket_subtype(m->packet) == JPACKET__INVISIBLE) {
		log_debug("handling invisible mode request");

		/* if we get this and we're available, it means go unavail first then reprocess this packet, nifty trick :) */
		if (oldpri >= -128) {
			js_session_from(m->s,
					jpacket_new(jutil_presnew
						    (JPACKET__UNAVAILABLE,
						     NULL,
						     xmlnode_get_tag_data(m->packet->x,"status")
						    )));
			js_session_from(m->s, m->packet);
			return M_HANDLED;
		}

		/* now, pretend we come online :) */
		/* (this is the handling of the reinjected invisible presence
		 * or an initial invisible presence) */
		mp->invisible = 1;
		mod_presence_roster(m, NULL);	/* send out probes to users we are subscribed to */
		m->s->priority = newpri;

		/* store presence in xdb? */
		if (mp->conf->pres_to_xdb > 0)
			mod_presence_store(m);

		xmlnode_free(m->packet->x);	/* we do not broadcast invisible presences without a to attribute */

		return M_HANDLED;
	}

	/* our new presence */
	pnew = xmlnode_dup(m->packet->x);

	/* store presence in xdb? */
	if (mp->conf->pres_to_xdb > 0)
		mod_presence_store(m);

	/* stamp the sessions presence */
	delay = xmlnode_insert_tag(pnew, "x");
	xmlnode_put_attrib(delay, "xmlns", NS_DELAY);
	xmlnode_put_attrib(delay, "from", jid_full(m->s->id));
	xmlnode_put_attrib(delay, "stamp", jutil_timestamp());

	/* our old presence */
	pold = m->s->presence;
	m->s->presence = pnew;
	m->s->priority = jutil_priority(pnew);

	log_debug("presence oldp %d newp %d", oldpri, m->s->priority);

	/* if we're going offline now, let everyone know */
	if (m->s->priority < -128) {
		/* jutil_priority returns -129 in case the "type" attribute is missing */
		if (!mp->invisible)
			/* bcc's don't get told if we were invisible */
			_mod_presence_broadcast(m->s, mp->conf->bcc, m->packet->x, NULL);
		_mod_presence_broadcast(m->s, mp->A, m->packet->x, NULL);
		_mod_presence_broadcast(m->s, mp->I, m->packet->x, NULL);

		/* reset vars */
		mp->invisible = 0;
		mp->A->next = NULL;
		mp->I = NULL;

		xmlnode_free(m->packet->x);
		xmlnode_free(pold);
		return M_HANDLED;
	}

	/* available presence updates, intersection of A and T */
	if (oldpri >= -128 && !mp->invisible) {
		_mod_presence_broadcast_trusted(m->s, mp->A, m->packet->x);
		xmlnode_free(m->packet->x);
		xmlnode_free(pold);
		return M_HANDLED;
	}

	/* at this point we're coming out of the closet */
	mp->invisible = 0;

	/* make sure we get notified for any presence about ourselves */
	/* XXX: the following is the original code: it caused existing presences
	 * to be stamped several times and the presence to be sent out twice.

	 pnew = jutil_presnew(JPACKET__PROBE,jid_full(m->s->uid),NULL);
	 xmlnode_put_attrib(pnew,"from",jid_full(m->s->uid));
	 js_session_from(m->s, jpacket_new(pnew));

	 */
	/* XXX: I think this should be okay as well: */

	/* send us all presences of our other resources */
	for (cur = m->user->sessions; cur != NULL; cur = cur->next) {
		pool pool_for_existing_presence = NULL;
		xmlnode duplicated_presence = NULL;
		jpacket packet = NULL;

		/* skip our own session (and sanity check) */
		if (cur == m->s || cur->presence == NULL) {
			continue;
		}

		/* send the presence to us: we need a new pool as js_session_to() will free the packet's pool  */
		pool_for_existing_presence = pool_new();
		duplicated_presence =
		    xmlnode_dup_pool(pool_for_existing_presence,
				     cur->presence);
		xmlnode_put_attrib(duplicated_presence, "to",
				   jid_full(m->user->id));
		packet = jpacket_new(duplicated_presence);
		js_session_to(m->s, packet);
	}

	/* probe s10ns and populate A */
	mod_presence_roster(m, mp->A);

	/* we broadcast this baby! */
	_mod_presence_broadcast(m->s, mp->conf->bcc, m->packet->x, NULL);
	_mod_presence_broadcast(m->s, mp->A, m->packet->x, NULL);

	xmlnode_free(m->packet->x);
	xmlnode_free(pold);

	return M_HANDLED;
}

/**
 * update the A and I list, because we send a new presence out
 *
 * If we sent out an invisible presence, add the destination to the I list.
 *
 * If we sent out an available presence, add the destination to the A list and remove from the I list.
 *
 * If we sent out an unavailable presence, remove from both A and I lists.
 *
 * @note this is our first callback for outgoing presences, mod_presence_out() is the second, that should be called afterwards
 *
 * @param m the mapi structure
 * @param arg pointer to the modpres structure containing the modules session data (especially the lists A and I)
 * @return M_IGNORE if the stanza is no presence, else always M_PASS
 */
/* session thread */
mreturn mod_presence_avails(mapi m, void *arg)
{
	modpres mp = (modpres) arg;

	if (m->packet->type != JPACKET_PRESENCE)
		return M_IGNORE;

	if (m->packet->to == NULL)
		return M_PASS;

	log_debug("track presence sent to jids");

	/* handle invisibles: put in I and remove from A */
	if (jpacket_subtype(m->packet) == JPACKET__INVISIBLE) {
		if (mp->I == NULL)
			mp->I = jid_new(m->s->p, jid_full(m->packet->to));
		else
			jid_append(mp->I, m->packet->to);
		mp->A = _mod_presence_whack(m->packet->to, mp->A);
		return M_PASS;
	}

	/* ensure not invisible from before */
	mp->I = _mod_presence_whack(m->packet->to, mp->I);

	/* avails to A */
	if (jpacket_subtype(m->packet) == JPACKET__AVAILABLE)
		jid_append(mp->A, m->packet->to);

	/* unavails from A */
	if (jpacket_subtype(m->packet) == JPACKET__UNAVAILABLE)
		mp->A = _mod_presence_whack(m->packet->to, mp->A);

	return M_PASS;
}

/**
 * callback, that gets called if a session ends
 *
 * The session manager has set the presence of the user to unavailable, we have to broadcast this presence
 * to everybody that thinks we are available.
 *
 * @param m the mapi structure
 * @param arg pointer to the modpres structure containing the lists for this session
 * @return always M_PASS
 */
/* thread safe, session thread */
mreturn mod_presence_avails_end(mapi m, void *arg)
{
	modpres mp = (modpres) arg;

	log_debug("avail tracker guarantee checker");

	/* send  the current presence (which the server set to unavail) */
	//    xmlnode_put_attrib(m->s->presence, "from",jid_full(m->s->id));
	_mod_presence_broadcast(m->s, mp->conf->bcc, m->s->presence, NULL);
	_mod_presence_broadcast(m->s, mp->A, m->s->presence, NULL);
	_mod_presence_broadcast(m->s, mp->I, m->s->presence, NULL);

	/* store presence in xdb? */
	if (mp->conf->pres_to_xdb > 0)
		mod_presence_store(m);

	return M_PASS;
}

/**
 * callback, that gets called if a new session is establisched, registers all session oriented callbacks
 *
 * This callback is responsible for initializing a new instance of the _modpres structure, that holds
 * the list of entites that know that a user is available.
 *
 * @param m the mapi structure
 * @param arg the list of JabberIDs that get a bcc of all presences
 * @return always M_PASS
 */
mreturn mod_presence_session(mapi m, void *arg)
{
	modpres_conf conf = (modpres_conf) arg;
	modpres mp;

	/* track our session stuff */
	mp = pmalloco(m->s->p, sizeof(_modpres));
	//    mp->A = jid_user(m->s->id);
	mp->A = m->s->uid;
	mp->conf = conf;	/* no no, it's ok, these live longer than us */

	js_mapi_session(es_IN, m->s, mod_presence_in, mp);
	js_mapi_session(es_OUT, m->s, mod_presence_avails, mp);	/* must come first, it passes, _out handles */
	js_mapi_session(es_OUT, m->s, mod_presence_out, mp);
	js_mapi_session(es_END, m->s, mod_presence_avails_end, mp);

	return M_PASS;
}

/**
 * deliver presence stanzas to local users
 *
 * This callback ignores all stanzas but presence stanzas. presences sent to a user (without specifying a resource) have to
 * be delivered to all sessions of this user.
 *
 * Presences sent to a specified resource are not handled.
 *
 * @param m the mapi structure
 * @param arg ignored/unused
 * @return M_IGNORED if not a presence stanza, M_PASS if the presence has not been handled, M_HANDLED if the presence has been handled
 */
/* thread safe with sem */
mreturn mod_presence_deliver(mapi m, void *arg)
{
	session cur;

	if (m->packet->type != JPACKET_PRESENCE)
		return M_IGNORE;

	log_debug("deliver phase");

	/* only if we HAVE a user, and it was sent to ONLY the user@server, and there is at least one session available */
	if (m->user != NULL && m->packet->to->resource == NULL && js_session_primary_all_sem(m->user) != NULL) {
		log_debug("broadcasting to %s", m->user->user);

		/* broadcast */
		for (cur = m->user->sessions; cur != NULL; cur = cur->next) {
			if (cur->priority < -128)
				continue;
			js_session_to(cur, jpacket_new(xmlnode_dup(m->packet->x)));
		}
		SEM_UNLOCK(m->user->sem);

		if (jpacket_subtype(m->packet) != JPACKET__PROBE) {
			/* probes get handled by the offline thread as well? */
			xmlnode_free(m->packet->x);
			return M_HANDLED;
		}
	}

	return M_PASS;
}

/**
 * init the module, register callbacks
 *
 * builds a list of JabberIDs where presences should be blind carbon copied to.
 * (Enclosing each in a <bcc/> element, which are contained in one <presence/>
 * element in the session manager configuration.)
 *
 * registers mod_presence_session() as a callback, that gets notified on new sessions
 * and mod_presence_deliver() as a callback to deliver presence stanzas locally.
 *
 * @param si the session manager instance
 */
JSM_FUNC void mod_presence(jsmi si)
{
	xmlnode cfg = js_config(si, "presence");
	modpres_conf conf =
	    (modpres_conf) pmalloco(si->p, sizeof(_modpres_conf));

	log_debug("init");

	for (cfg = xmlnode_get_firstchild(cfg); cfg != NULL;
	     cfg = xmlnode_get_nextsibling(cfg)) {
		char *element_name = NULL;

		if (xmlnode_get_type(cfg) != NTYPE_TAG)
			continue;

		element_name = xmlnode_get_name(cfg);
		if (j_strcmp(element_name, "bcc") == 0) {
			if (conf->bcc == NULL)
				conf->bcc =
				    jid_new(si->p, xmlnode_get_data(cfg));
			else
				jid_append(conf->bcc,
					   jid_new(si->p,
						   xmlnode_get_data(cfg)));
		} else if (j_strcmp(element_name, "presence2xdb") == 0) {
			conf->pres_to_xdb++;
		}
	}

	js_mapi_register(si, e_DELIVER, mod_presence_deliver, NULL);
	js_mapi_register(si, e_SESSION, mod_presence_session,
			 (void *) conf);
}
