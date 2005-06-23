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

  add this in config file

<stats>
   <allow_all/>  add this tag if you want everybody to get server stats
</stats>

 * --------------------------------------------------------------------------*/
#include "jsm.h"

#define NS_STATS "http://jabber.org/protocol/stats"

#define STATS_COUNT 6

static char * available_stats[] = {
  "time/uptime",
  "users/online",
  "users/max_online_today",
  "users/max_online_yesterday",
  "users/registered_today",
  "users/registered_from_start",
  "bandwidth/packets-in",
  "bandwidth/packets-out",
#ifndef WIN32
  "memory/usage",
#endif
  NULL
};

#ifndef WIN32
/* memory usage , return bytes */
#define PROC_FILE_BUF 1023
long get_memory_usage(){
  char buf[PROC_FILE_BUF+1];
  int fd,len;
  long mem;
  int size,resident, shared, trs, drs, lrs, dt;

  snprintf(buf,100,"/proc/%d/statm",getpid());
  fd  = open(buf,O_RDONLY);
  len = read(fd, buf, PROC_FILE_BUF);
  close(fd);
  buf[len]=0;

  fd = sscanf(buf,"%d %d %d %d %d %d %d",
			  &size, &resident, &shared, &trs, &drs, &lrs, &dt);

  mem = resident;
  mem <<= 12;

  return mem;
}
#endif

mreturn mod_stats_server(mapi m, void *arg){
  xmlnode cur;
  int i;

  if (m->packet->type != JPACKET_IQ) return M_IGNORE;
  if (jpacket_subtype(m->packet) != JPACKET__GET) return M_PASS;
  if (!NSCHECK(m->packet->iq,NS_STATS)) return M_PASS;
  if (m->packet->to->resource) return M_PASS;

  /* get data from the config file */
  i = 0;
  if (xmlnode_get_tag(js_config(m->si,"stats"),"allow_all") != NULL)
	i = 1;

  log_debug("handling stats get %s",jid_full(m->packet->from));

  /* check if admin */
  if ((i==0)&&
	  (!js_admin_jid(m->si,jid_user(m->packet->from),ADMIN_READ))){
	jutil_error(m->packet->x,TERROR_AUTH);
	jpacket_reset(m->packet);
	js_deliver(m->si,m->packet);
	return M_HANDLED;
  }

  /* check if any stat have given iq query */
  cur = xmlnode_get_firstchild(m->packet->iq);
  for(;cur != NULL; cur = xmlnode_get_nextsibling(cur)){
	if (xmlnode_get_type(cur) != NTYPE_TAG) continue;
	break;
  }
  if (cur != NULL)
	cur = xmlnode_get_firstchild(m->packet->iq);

  jutil_tofrom(m->packet->x);
  xmlnode_put_attrib(m->packet->x,"type","result");

  /* return available stats */
  if (!cur){
	for (i=0;available_stats[i];i++){
	  xmlnode_put_attrib(xmlnode_insert_tag(m->packet->iq,"stat"),
						 "name",available_stats[i]);
	}
	jpacket_reset(m->packet);
	js_deliver(m->si,m->packet);
	return M_HANDLED;
  }

  /* return server stats */
  /* cur is already first stat */
  for(;cur != NULL; cur = xmlnode_get_nextsibling(cur)){
	char *name;
	char buf[31];
	int found;

	if (xmlnode_get_type(cur) != NTYPE_TAG) continue;
	if (j_strcmp(xmlnode_get_name(cur),"stat") != 0) continue;

	name = xmlnode_get_attrib(cur, "name");

	if (!name) continue;

	log_debug("get stats for %s", name);

	found = 0;
	for (i=0;available_stats[i];i++){

	  if (j_strcmp(available_stats[i], name) != 0) continue;

	  log_debug("stats for %s", name);
	  /* give stats */

	  found = 1;

	  /* time/uptime */
	  if (j_strcmp(name,"time/uptime")==0){
		snprintf(buf,30,"%d",time(NULL) - m->si->stats->started);
		xmlnode_put_attrib(cur,"value",buf);
		xmlnode_put_attrib(cur,"units","seconds");
	  }

	  /* users/online */
	  if (j_strcmp(name,"users/online")==0){
		snprintf(buf,30,"%d",m->si->stats->sessioncount);
		xmlnode_put_attrib(cur,"value",buf);
		xmlnode_put_attrib(cur,"units","users");
	  }

	  if (j_strcmp(name,"users/max_online_today")==0){
		snprintf(buf,30,"%d",m->si->stats->session_max_today);
		xmlnode_put_attrib(cur,"value",buf);
		xmlnode_put_attrib(cur,"units","users");
	  }

	  if (j_strcmp(name,"users/max_online_yesterday")==0){
		snprintf(buf,30,"%d",m->si->stats->session_max_yesterday);
		xmlnode_put_attrib(cur,"value",buf);
		xmlnode_put_attrib(cur,"units","users");
	  }

	  if (j_strcmp(name,"users/registered_today")==0){
		snprintf(buf,30,"%d",m->si->stats->users_registered_today);
		xmlnode_put_attrib(cur,"value",buf);
		xmlnode_put_attrib(cur,"units","users");
	  }

	  if (j_strcmp(name,"users/registered_from_start")==0){
		snprintf(buf,30,"%d",m->si->stats->users_registered_from_start);
		xmlnode_put_attrib(cur,"value",buf);
		xmlnode_put_attrib(cur,"units","users");
	  }

	  if (j_strcmp(name,"bandwidth/packets-in")==0){
		snprintf(buf,30,"%lu",m->si->stats->packets_in);
		xmlnode_put_attrib(cur,"value",buf);
		xmlnode_put_attrib(cur,"units","packets");
	  }

	  if (j_strcmp(name,"bandwidth/packets-out")==0){
		snprintf(buf,30,"%lu",m->si->stats->packets_out);
		xmlnode_put_attrib(cur,"value",buf);
		xmlnode_put_attrib(cur,"units","packets");
	  }
#ifndef WIN32
	  if (j_strcmp(name,"memory/usage")==0){
		long mem = get_memory_usage();
		if (mem > 0){
		  snprintf(buf,30,"%lu",mem);
		  xmlnode_put_attrib(cur,"value",buf);
		  xmlnode_put_attrib(cur,"units","bytes");
		}
		else
		  found = 0;
	  }
#endif
	  break;
	}

	if (found <= 0){
	  xmlnode err;
	  err = xmlnode_insert_tag(cur,"error");
	  xmlnode_put_attrib(err,"code","404");
	  xmlnode_insert_cdata(err,"Not Found",-1);
	}
  }

  jpacket_reset(m->packet);
  js_deliver(m->si,m->packet);
  return M_HANDLED;
}

/* stats mod based on JEP-39 */
JSM_FUNC void mod_stats(jsmi si){
  js_mapi_register(si, e_SERVER, mod_stats_server, NULL);
}
