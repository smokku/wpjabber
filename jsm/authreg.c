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
 * service.c - Service API
 *
 * --------------------------------------------------------------------------*/

/*
   Modifed by £ukasz Karwacki <lukasm@wp-sa.pl>

changes
  added user->ref-- after js_user

*/

#include "jsm.h"

#ifdef FORWP
int js_register_ldap_user(udata user);
udata js_ldap_user(jsmi si, jid to);
#endif

/* authreg threads */
void js_authreg_thread_hander(void *arg, void *value)
{
	jsmi si = (jsmi) arg;
	packet_thread_p pt = (packet_thread_p) value;
	jpacket p = pt->jp;
	udata user;
	char *ul;
	xmlnode x;


	log_debug("Handle Authreg packet");

	/* enforce the username to lowercase */
	if (p->to->user != NULL)
		for (ul = p->to->user; *ul != '\0'; ul++)
			*ul = tolower(*ul);

	if (p->to->user != NULL
	    && (jpacket_subtype(p) == JPACKET__GET
		|| p->to->resource != NULL) && NSCHECK(p->iq, NS_AUTH)) {
		/* is this a valid auth request? */

		log_debug("auth request");

		if (jpacket_subtype(p) == JPACKET__GET) {
			if (!js_mapi_call(si, e_AUTH, p, NULL, NULL)) {
				xmlnode_insert_tag(p->iq, "resource");
				/* of course, resource is required :) */
				xmlnode_put_attrib(p->x, "type", "result");
				jutil_tofrom(p->x);
			}
		} else {
#ifdef FORWP
			int ldap_user = 0;
#endif
			/* attempt to fetch user data based on the username */
			user = js_user(si, p->to, 0);
#ifdef FORWP
			if (!user) {
				log_debug("Try getting user from ldap");

				ldap_user = 1;
				user = js_ldap_user(si, p->to);
			}
#endif
			if (user) {
				log_debug("Got user");

				if (!js_mapi_call
				    (si, e_AUTH, p, user, NULL)) {
					log_debug("Bad auth");
					jutil_error(p->x, TERROR_AUTH);
				}
#ifdef FORWP
				if (ldap_user) {
					//if result
					if (j_strcmp
					    (xmlnode_get_attrib
					     (p->x, "type"),
					     "result") == 0) {
						log_debug
						    ("OKI. register new user.");
						//save user on disk after good login
						js_register_ldap_user
						    (user);
					}
				}
#endif
				THREAD_DEC(user->ref);
			}	//user
			else {
				log_debug("NO user");
				jutil_error(p->x, TERROR_AUTH);
			}
		}		//jpacket GET
	} else if (NSCHECK(p->iq, NS_REGISTER)) {	/* is this a registration request? */
		if (jpacket_subtype(p) == JPACKET__GET) {
			log_debug("registration get request");
			/* let modules try to handle it */
			if (!js_mapi_call(si, e_REGISTER, p, NULL, NULL)) {
				jutil_error(p->x, TERROR_NOTIMPL);
			} else {	/* make a reply and the username requirement is built-in :) */
				xmlnode_put_attrib(p->x, "type", "result");
				jutil_tofrom(p->x);
				xmlnode_insert_tag(p->iq, "username");
			}
		} else {
			log_debug("registration set request");
			if (p->to->user == NULL
			    || xmlnode_get_tag_data(p->iq,
						    "password") == NULL) {
				jutil_error(p->x, TERROR_NOTACCEPTABLE);
			} else if ((user = js_user(si, p->to, 0)) != NULL) {
				THREAD_DEC(user->ref);
				jutil_error(p->x,
					    TERROR_USERNAMENOTAVAILABLE);
			} else
			    if (!js_mapi_call
				(si, e_REGISTER, p, NULL, NULL)) {
				jutil_error(p->x, TERROR_NOTIMPL);
			}
		}

	} else {		/* unknown namespace or other problem */
		jutil_error(p->x, TERROR_NOTACCEPTABLE);
	}

	/* restore the route packet */
	x = xmlnode_wrap(p->x, "route");
	xmlnode_put_attrib(x, "from", xmlnode_get_attrib(p->x, "from"));
	xmlnode_put_attrib(x, "to", xmlnode_get_attrib(p->x, "to"));
	xmlnode_put_attrib(x, "type", xmlnode_get_attrib(p->x, "route"));
	/* hide our uglies */
	xmlnode_hide_attrib(p->x, "from");
	xmlnode_hide_attrib(p->x, "to");
	xmlnode_hide_attrib(p->x, "route");
	/* reply */
	deliver(dpacket_new(x), si->i);
}
