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
<mod_version>
   <name>JSM</name>
   <version> ... </version>
   <core_version> ... </core_version>
   <os> ... </os>
   <no_os_version/>
</mod_version>


 * --------------------------------------------------------------------------*/
#include "jsm.h"
#ifndef WIN32
#include <sys/utsname.h>
#endif

mreturn mod_version_reply(mapi m, void *arg){
  xmlnode version = (xmlnode)arg;

    if(m->packet->type != JPACKET_IQ) return M_IGNORE;
    if(!NSCHECK(m->packet->iq,NS_VERSION) ||
	   m->packet->to->resource != NULL) return M_PASS;

    /* first, is this a valid request? */
    if(jpacket_subtype(m->packet) != JPACKET__GET){
	js_bounce(m->si,m->packet->x,TERROR_NOTALLOWED);
	return M_HANDLED;
    }

    log_debug("mod_version","handling query from",jid_full(m->packet->from));

    jutil_iqresult(m->packet->x);
	xmlnode_insert_tag_node(m->packet->x, version);
    jpacket_reset(m->packet);

    js_deliver(m->si,m->packet);

    return M_HANDLED;
}

JSM_FUNC void mod_version(jsmi si){
  xmlnode version,os,config;
#ifndef WIN32
  struct utsname un;
#endif
  char *n,*v,*o,*cv;
  char buf[401];

  log_debug("mod_version","init");

  config = js_config(si,"mod_version");
  n  = xmlnode_get_tag_data(config, "name");
  v  = xmlnode_get_tag_data(config, "version");
  cv = xmlnode_get_tag_data(config, "core_version");
  o  = xmlnode_get_tag_data(config, "os");

  version = xmlnode_new_tag("query");
  xmlnode_put_attrib(version, "xmlns", NS_VERSION);
  xmlnode_insert_cdata(xmlnode_insert_tag(version,"name"),
					   n ? n : "jsm",3);
  xmlnode_insert_cdata(xmlnode_insert_tag(version,"version"),
					   v ? v : MOD_VERSION,-1);
  xmlnode_insert_cdata(xmlnode_insert_tag(version,"core_version"),
					   cv ? cv : VERSION,-1);

  if (!xmlnode_get_tag(config, "no_os_version")){
	os = xmlnode_insert_tag(version,"os");
	if (o){
	  xmlnode_insert_cdata(os, o ,-1);
	}
	else{
#ifdef WIN32
	  xmlnode_insert_cdata(os,"WIN",-1);
#else
	  uname(&un);
	  snprintf(buf,400,"%s %s",un.sysname,un.release);
	  xmlnode_insert_cdata(os,buf,-1);
#endif
	}
  }

  js_mapi_register(si, e_SERVER, mod_version_reply, (void*)version);
}
