/*



<deny_new/>
<auth tag='specialauthtag'/>

    log module
    Author: £ukasz Karwacki.

<packet_stats>
<file domain='jabber.wp.pl'>log/packet_jabber</file>
<file domain='icq.jabber.wp.pl'>log/packet_icq</file>
<file>log/packet_other</file>
<interval>120</interval>
</packet_stats>

 * --------------------------------------------------------------------------*/
#include "jsm.h"

#define DEFAULT_STATS_INTERVAL 120

typedef struct log_stats_struct {
	char *domain;
	char *file;

	volatile unsigned int messages;
	volatile unsigned int presences;
	volatile unsigned int queries;

#ifdef FORWP
	volatile unsigned int files;
	volatile unsigned int voice_message;
	volatile unsigned int voip;
#endif
	struct log_stats_struct *next;
} _log_stats, *log_stats;

typedef struct log_packet_struct {
	log_stats stats;

	int time;
} _log_packet, *log_packet;

/** beat */
result mod_packet_stats_beat(void *arg)
{
	FILE *f;
	char date[101], buf[201], stamp[101];
	struct tm today;
	unsigned long ltime;
	unsigned int msg, pres, query;
#ifdef FORWP
	unsigned int files, voip, vm;
#endif
	log_packet lp = (log_packet) arg;
	log_stats sp;

	time(&ltime);
	localtime_r(&ltime, &today);

	strftime((char *) date, 100, "%Y_%m_%d", &today);
	snprintf(stamp, 100, "%d%02d%02dT%02d:%02d:%02d",
		 1900 + today.tm_year,
		 today.tm_mon + 1, today.tm_mday, today.tm_hour,
		 today.tm_min, today.tm_sec);


	for (sp = lp->stats; sp; sp = sp->next) {

		msg = sp->messages;
		sp->messages = 0;
		pres = sp->presences;
		sp->presences = 0;
		query = sp->queries;
		sp->queries = 0;
#ifdef FORWP
		files = sp->files;
		sp->files = 0;
		voip = sp->voip;
		sp->voip = 0;
		vm = sp->voice_message;
		sp->voice_message = 0;
#endif

		snprintf(buf, 200, "%s_%s.log", sp->file, date);

		f = fopen(buf, "at");
		if (f) {
#ifdef FORWP
			fprintf(f,
				"%s m:\t%u\tp:\t%u\tq:\t%u\tf:\t%u\tvo:\t%u\tvm:\t%u\r\n",
				stamp, msg, pres, query, files, voip, vm);
#else
			fprintf(f, "%s m:\t%u\tp:\t%u\tq:\t%u\r\n",
				stamp, msg, pres, query);
#endif
			fclose(f);
		}
	}
	return r_DONE;
}

/** user packets */
mreturn mod_packet_stats_out(mapi m, void *arg)
{
	log_packet lp = (log_packet) arg;
	log_stats sp;

	if (!m->packet->to)
		sp = lp->stats;
	else {
		for (sp = lp->stats; sp; sp = sp->next) {
			if (!sp->domain)
				break;
			if (j_strcmp(sp->domain, m->packet->to->server) ==
			    0)
				break;
		}
	}

	// queries at the begining, most packets are queries
	if (m->packet->type == JPACKET_IQ) {
		THREAD_INC(sp->queries);
		return M_PASS;
	}

	if (m->packet->type == JPACKET_PRESENCE) {
		THREAD_INC(sp->presences);
		return M_PASS;
	}

	if (m->packet->type == JPACKET_MESSAGE) {
#ifdef FORWP
		xmlnode x;
#endif
		THREAD_INC(sp->messages);

#ifdef FORWP
		for (x = xmlnode_get_tag(m->packet->x, "x"); x;
		     x = xmlnode_get_nextsibling(x)) {

			if (xmlnode_get_type(x) != NTYPE_TAG)
				continue;
			if (j_strcmp(xmlnode_get_name(x), "x"))
				continue;

			if (j_strcmp
			    (xmlnode_get_attrib(x, "xmlns"),
			     "wpmsg:x:voice") == 0) {
				log_debug("voice message");

				THREAD_INC(sp->voice_message);
				return M_PASS;
			}

			if (j_strcmp(xmlnode_get_attrib(x, "type"), "req")
			    == 0) {
				char *service;
				service =
				    xmlnode_get_tag_data(x, "service");

				log_debug("request");

				if (j_strcmp(service, "FileServer") == 0) {
					log_debug("file");
					THREAD_INC(sp->files);
					return M_PASS;
				}

				if (j_strcmp(service, "voiphlp") == 0) {
					log_debug("voip");
					THREAD_INC(sp->voip);
					return M_PASS;
				}
			}

		}
#endif


		return M_PASS;
	}


	return M_PASS;
}

mreturn mod_packet_stats_session(mapi m, void *arg)
{
	js_mapi_session(es_OUT, m->s, mod_packet_stats_out, arg);
	return M_PASS;
}


/* stats mod based on JEP-39 */
JSM_FUNC void mod_packet_stats(jsmi si)
{
	xmlnode temp;

	log_debug("init");

	/* read config */
	temp = xmlnode_get_tag(si->config, "packet_stats");
	if (temp) {
		log_packet lp;
		log_stats sp;
		xmlnode cur;
		int count = 0;
		int nodomain = 0;

		lp = pmalloco(si->p, sizeof(_log_packet));
		lp->time =
		    j_atoi(xmlnode_get_tag_data(temp, "interval"),
			   DEFAULT_STATS_INTERVAL);

		log_debug("stats %d", lp->time);

		for (cur = xmlnode_get_tag(temp, "file"); cur;
		     cur = xmlnode_get_nextsibling(cur)) {
			char *file, *domain;

			if (xmlnode_get_type(cur) != NTYPE_TAG)
				continue;

			if (j_strcmp(xmlnode_get_name(cur), "file"))
				continue;


			file = xmlnode_get_data(cur);
			domain = xmlnode_get_attrib(cur, "domain");

			log_debug("config_packet_stats %s  %s %s",
				  xmlnode2str(cur), file,
				  domain ? domain : "other");

			if (file) {
				sp = pmalloco(si->p, sizeof(_log_stats));
				sp->file = pstrdup(si->p, file);

				if (domain) {
					log_debug("mod_packet_stats %s %s",
						  file, domain);

					sp->domain =
					    pstrdup(si->p, domain);

					//add at the begining or on the second place
					if (j_strcmp(sp->domain, si->host)
					    == 0) {
						sp->next = lp->stats;
						lp->stats = sp;
					} else {
						if (!lp->stats)
							lp->stats = sp;
						else {
							//if other then go first
							if (!lp->stats->
							    domain) {
								sp->next =
								    lp->
								    stats;
								lp->stats =
								    sp;
							} else {
								//go second
								sp->next =
								    lp->
								    stats->
								    next;
								lp->stats->
								    next =
								    sp;
							}
						}
					}
				} else {
					if (nodomain) {
						log_alert
						    ("mod_packet_stats",
						     "error: two global logs");
						return;
					}
					nodomain = 1;

					log_debug
					    ("mod_packet_stats %s other",
					     file);

					sp->domain = NULL;

					//add at the end
					if (!lp->stats)
						lp->stats = sp;
					else {
						log_stats tmp;
						for (tmp = lp->stats;
						     tmp->next;
						     tmp = tmp->next);
						tmp->next = sp;
					}
				}
				count++;
			}
		}


		if (count) {
			for (sp = lp->stats; sp; sp = sp->next) {
				log_debug("config_packet_stats %s %s",
					  sp->file,
					  (sp->domain ? sp->
					   domain : "other"));
			}

			js_mapi_register(si, e_SESSION,
					 mod_packet_stats_session,
					 (void *) lp);
			register_beat(lp->time, mod_packet_stats_beat,
				      (void *) lp);
		}
	}
}
