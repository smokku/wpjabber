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
 * server.c - thread that handles messages/packets intended for the server:
 *	      administration, public IQ (agents, etc)
 * --------------------------------------------------------------------------*/
/*
   Modifed by �ukasz Karwacki <lukasm@wp-sa.pl>

changes
 1. added thread_dec after js_user


*/

#include "jsm.h"

void js_server_main(jsmi si, jpacket jp)
{
	udata u = NULL;

	log_debug("THREAD:SERVER received a packet: %s",
		  xmlnode2str(jp->x));

	/* get the user struct for convience if the sender was local */
	if (js_islocal(si, jp->from))
		u = js_user(si, jp->from, 0);

	/* let the modules have a go at the packet; if nobody handles it... */
	if (!js_mapi_call(si, e_SERVER, jp, u, NULL))
		js_bounce(si, jp->x, TERROR_NOTFOUND);

	if (u != NULL) {
		THREAD_DEC(u->ref);
	}
}
