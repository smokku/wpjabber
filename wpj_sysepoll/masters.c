/* --------------------------------------------------------------------------
 *
 *  WP
 *
 * --------------------------------------------------------------------------*/


#include "wpj.h"

extern wpjs wpj;
extern ios io__data;
char hashbuf[41];

void close_client(io m);

void server_process_xml(io m, int state, void *arg, xmlnode x)
{
	server_info si = (server_info) arg;
	cqueue q, q2;
	xmlnode cur;
	xmlnode rt;
	xmlnode ox;
	io socket, temp;
	client_c ci;
	int loc;
	log_debug("process XML: m:%X state:%d, arg:%X, x:%X", m, state,
		  arg, x);


	switch (state) {
	case IO_NEW:
		si->m = m;

		/* Send a stream header to the server */
		log_debug("base_connecting to %s: ", si->mhost);

		cur =
		    xstream_header("jabber:component:accept",
				   wpj->cfg->hostname, NULL);
		io_write(m, NULL, xstream_header_char(cur), -1);
		log_debug("Stream opened (%s)", xstream_header_char(cur));
		xmlnode_free(cur);

		si->state = conn_OPEN;
		io__data->serverio = m;
		return;

	case IO_XML_ROOT:
		/* Extract stream ID and generate a key to hash */
		shahash_r(spools
			  (x->p, xmlnode_get_attrib(x, "id"), si->secret,
			   x->p), hashbuf);

		/* Build a handshake packet */
		cur = xmlnode_new_tag("handshake");
		xmlnode_insert_cdata(cur, hashbuf, -1);

		/* Transmit handshake */
		io_write(m, NULL, xmlnode2str(cur), -1);
		log_debug("Handshake packet sent(%s)", xmlnode2str(cur));

		xmlnode_free(cur);
		xmlnode_free(x);
		return;

	case IO_ERROR:
		si->state = conn_CLOSED;
		io__data->serverio->state = state_CLOSE;
		log_debug("Server  : Got Error");
		return;

	case IO_XML_NODE:
		/* Only deliver packets after handshake */
		if (si->state == conn_AUTHD || si->state == conn_CLOSING) {
			jid jsto;

			/* only routed packets */
			ox = x;
			if (j_strcmp(xmlnode_get_name(x), "route") == 0) {
				rt = x;
				x = xmlnode_get_firstchild(rt);
			} else {
				if (j_strcmp
				    (xmlnode_get_name(x),
				     "endconnection") == 0) {
					log_debug
					    ("Server is closing. stop sending to him");

					si->state = conn_CLOSING;

					for (socket =
					     io__data->master__list;
					     socket != NULL;) {
						ci = &(wpj->
						       clients[socket->
							       pollpos]);

						if ((ci->state ==
						     state_REFRESHING_SESSION)
						    || (socket->state !=
							state_ACTIVE)) {
							socket =
							    socket->next;
							continue;
						}

						if ((ci->state ==
						     state_AUTHD)
						    && (ci->real_user.
							username)
						    && (ci->real_user.
							resource)
						    && (wpj->cfg->
							auto_refresh_sessions))
						{
							/* set status refresh */
							ci->state =
							    state_REFRESHING_SESSION;
						} else {
							/* close not authed connections */
							ci->state =
							    state_CLOSING;
							if (socket->
							    accepted) {
								io_write
								    (socket,
								     NULL,
								     "<stream:error>Server temporarily unavailable</stream:error></stream:stream>",
								     -1);
							}
							temp = socket;
							socket =
							    socket->next;
							io_close(temp);
							usleep(10);
							close_client(temp);
							continue;
						}
						socket = socket->next;
					}

				}
				xmlnode_free(ox);
				return;
			}

			/* check id */
			jsto =
			    jid_new(xmlnode_pool(rt),
				    xmlnode_get_attrib(rt, "to"));
			if (jsto == NULL || jsto->user == NULL) {
				xmlnode_free(ox);
				return;
			}

			/* user number */
			loc = atoi(jsto->user);

			/* we don't trust server */
			if ((loc < 2) || (loc >= DEF_MAX_CLIENTS)) {
				log_alert("server",
					  "sends bad data ! loc=%d", loc);
				xmlnode_free(ox);
				return;
			}

			/* if client is closed */
			if (wpj->clients[loc].state == state_CLOSING) {

				if (j_strcmp
				    (xmlnode_get_attrib(rt, "type"),
				     "session") == 0) {
					log_alert("server",
						  "sessions packet for closing user");
					jutil_tofrom(ox);
					xmlnode_put_attrib(ox, "type",
							   "error");
					io_write(si->m, ox, NULL, 0);
					return;
				}

				if (j_strcmp
				    (xmlnode_get_attrib(rt, "type"),
				     "sessioncheck") == 0) {
					jutil_tofrom(ox);
					xmlnode_put_attrib(ox, "type",
							   "error");
					io_write(si->m, ox, NULL, 0);
					return;
				}

				jutil_tofrom(ox);
				xmlnode_put_attrib(ox, "type", "error");
				io_write(si->m, ox, NULL, 0);
				return;
			}

			/* return to server if not our resource */
			if (j_strcmp
			    (wpj->clients[loc].res, jsto->resource)) {
				jutil_tofrom(ox);
				xmlnode_put_attrib(ox, "type", "error");

				io_write(si->m, ox, NULL, 0);
				return;
			}

			/* session check */
			if (j_strcmp
			    (xmlnode_get_attrib(rt, "type"),
			     "sessioncheck") == 0) {
				xmlnode_free(rt);
				return;
			}

			if (j_strcmp
			    (xmlnode_get_attrib(rt, "type"),
			     "error") == 0) {
				xmlnode h;
				xmlnode errx;
				char *err;

				/* if client oki */
				if (wpj->clients[loc].state !=
				    state_CLOSING) {
					wpj->clients[loc].state =
					    state_CLOSING;
					errx =
					    xmlnode_get_tag(rt, "error");
					err =
					    xmlnode_get_attrib(rt,
							       "error");
					h = xmlnode_new_tag_pool
					    (xmlnode_pool(rt),
					     "stream:error");
					xmlnode_insert_cdata(h, err,
							     strlen(err));
					io_write(wpj->clients[loc].m, h,
						 NULL, 0);
					io_close(wpj->clients[loc].m);
					// close_client(wpj->clients[loc].m);
				} else {
					xmlnode_free(ox);
				}
				return;
			}

			/* session message */
			if ((wpj->clients[loc].state == state_UNKNOWN
			     || wpj->clients[loc].state ==
			     state_REFRESHING_SESSION)

			    && j_strcmp(xmlnode_get_attrib(rt, "type"),
					"session") == 0) {
				int send_presence =
				    (wpj->clients[loc].state ==
				     state_REFRESHING_SESSION);

				wpj->clients[loc].host =
				    jid_new(wpj->clients[loc].m->p,
					    xmlnode_get_attrib(rt,
							       "from"));

				wpj->clients[loc].state = state_AUTHD;

				log_debug("connection authd");

				if (send_presence
				    && wpj->clients[loc].real_user.
				    presence) {
					log_debug("Sending presence copy");

					client_process_xml(wpj->
							   clients[loc].m,
							   IO_XML_NODE,
							   (void *) &(wpj->
								      clients
								      [loc]),
							   xmlnode_dup
							   (wpj->
							    clients[loc].
							    real_user.
							    presence));
				}

				/* if errro */
				if (wpj->clients[loc].m->state ==
				    state_CLOSE) {
					close_client(wpj->clients[loc].m);
					xmlnode_free(ox);
					return;
				}

				log_debug("Flush the client queue...");
				q = wpj->clients[loc].q_start;
				while (q != NULL) {
					q2 = q->next;
					client_process_xml(wpj->
							   clients[loc].m,
							   IO_XML_NODE,
							   (void *) &(wpj->
								      clients
								      [loc]),
							   q->x);
					/* if error */
					if (wpj->clients[loc].m->state ==
					    state_CLOSE) {
						wpj->clients[loc].q_start =
						    q2;
						close_client(wpj->
							     clients[loc].
							     m);
						xmlnode_free(ox);
						return;
					}

					q = q2;
				}
				wpj->clients[loc].q_start = NULL;
				wpj->clients[loc].q_end = NULL;

				xmlnode_free(ox);
				return;
			}

			/* message to not authorised client */
			if ((wpj->clients[loc].state == state_UNKNOWN ||
			     wpj->clients[loc].state ==
			     state_REFRESHING_SESSION)

			    && j_strcmp(xmlnode_get_attrib(rt, "type"),
					"auth") == 0) {
				char *type;
				char *id;

				type = xmlnode_get_attrib(x, "type");
				id = xmlnode_get_attrib(x, "id");

				if ((j_strcmp(type, "result") == 0) &&
				    j_strcmp(wpj->clients[loc].auth_id,
					     id) == 0) {
					xmlnode sx;

					/* komunikat rozpoczecia sesji */
					sx = xmlnode_new_tag("route");
					xmlnode_put_attrib(sx, "from",
							   wpj->
							   clients[loc].
							   id);
					xmlnode_put_attrib(sx, "to",
							   jid_full(wpj->
								    clients
								    [loc].
								    orghost));
					xmlnode_put_attrib(sx, "type",
							   "session");

					/* SESSION IP SENDING to have it in logs on server */
					xmlnode_put_attrib(sx, "ip",
							   wpj->
							   clients[loc].m->
							   ip);

					io_write(si->m, sx, NULL, 0);

					log_alert("Session",
						  "%p [%s] (%d)",
						  wpj->clients[loc].m,
						  jid_full(wpj->
							   clients[loc].
							   orghost),
						  wpj->clients_count);
				}

				/* do not inform client after session refresh */
				if (wpj->clients[loc].state ==
				    state_REFRESHING_SESSION)
					x = NULL;
				else
					/* SESSION IP SENDING - send this IP to client but why ??
					   to make him know that he has external IP
					   and can make client-client connections */
					xmlnode_put_attrib(x, "ip",
							   wpj->
							   clients[loc].m->
							   ip);
			}

			/* any other message */
			if (x != NULL)
				io_write(wpj->clients[loc].m, x, NULL, 0);
			else
				xmlnode_free(ox);

			return;
		}


		/* If a handshake packet is recv'd from the server,
		   we have successfully auth'd
		   go ahead and update the connection state */
		if (j_strcmp(xmlnode_get_name(x), "handshake") == 0) {
			char *stime;
			int reconnect = 1;
			si->state = conn_AUTHD;

			stime = xmlnode_get_attrib(x, "time");

			if (stime != NULL) {
				loc = strlen(stime);
				if ((si->servertime[0])
				    &&
				    (j_strncmp(stime, si->servertime, loc)
				     == 0))
					reconnect = 0;

				memcpy(si->servertime, stime, loc + 1);
				log_alert("Server",
					  "Handshake OK. >%s< %d",
					  si->servertime, reconnect);
			} else {
				log_alert("Server", "Handshake OK.");
			}


			/* reconnect all clients */
			loc = 0;
			for (socket = io__data->master__list;
			     socket != NULL;) {
				/* do not refresh closing clients */
				if (socket->state != state_ACTIVE) {
					socket = socket->next;
					continue;
				}

				ci = &(wpj->clients[socket->pollpos]);
				if (ci->state == state_REFRESHING_SESSION) {
					if (reconnect) {
						/* send auth packet for this client */
						xmlnode q, h;
						loc++;
						/* every 40 sends */
						if (loc > 40) {	/* 200 bajtow, soket na 8kb -> 8kb/200 = 5*8 = 40 */
							loc = 0;
							usleep(500);	/* wait for packets to go out */
							/* 1 s -> 2000 * 40 = 80 tys clients */
						}
						/* build packet */
						h = jutil_iqnew
						    (JPACKET__SET,
						     NS_AUTH);
						q = xmlnode_get_tag(h,
								    "query");
						xmlnode_insert_cdata
						    (xmlnode_insert_tag
						     (q, "username"),
						     ci->real_user.
						     username, -1);
						xmlnode_insert_cdata
						    (xmlnode_insert_tag
						     (q, "resource"),
						     ci->real_user.
						     resource, -1);
						xmlnode_insert_tag(q,
								   "trickypassword");

						xmlnode_put_attrib(h, "id",
								   ci->
								   auth_id);

						rt = xmlnode_wrap(h,
								  "route");
						xmlnode_put_attrib(rt, "from", ci->id);	//wpj id
						xmlnode_put_attrib(rt,
								   "to",
								   jid_full
								   (ci->
								    orghost));
						xmlnode_put_attrib(rt,
								   "type",
								   "auth");


						log_debug
						    ("tricky authorisation packet created: %s",
						     xmlnode2str(rt));

						if (si->m->state !=
						    state_ACTIVE)
							break;

						io_write(si->m, rt, NULL,
							 0);
					} else {
						/* set state */
						ci->state = state_AUTHD;

						/* process old packets */
						loc = 0;
						q = ci->q_start;
						while (q != NULL) {

							q2 = q->next;
							client_process_xml
							    (ci->m,
							     IO_XML_NODE,
							     ci, q->x);

							/* if error */
							if (ci->m->state ==
							    state_CLOSE) {
								ci->q_start
								    = q2;
								close_client
								    (ci->
								     m);
								loc = 1;
								break;
							}

							q = q2;
						}
						if (!loc) {
							ci->q_start = NULL;
							ci->q_end = NULL;
						}

					}
				}
				socket = socket->next;
			}
		}

		if (x != NULL)
			xmlnode_free(x);
		return;

	case IO_CLOSED:
		if (wpj_shutdown > 0)
			return;
		log_alert("server", "connection down. reconnect.");

		si->state = conn_CLOSED;
		si->m = NULL;

		/* only after NEW process this */
		if (io__data->serverio != NULL) {
			io__data->serverio = NULL;

			/* loop sockets
			   close socket with no auth
			   set status to refresh */
			for (socket = io__data->master__list;
			     socket != NULL;) {

				ci = &(wpj->clients[socket->pollpos]);

				if ((ci->state == state_REFRESHING_SESSION)
				    || (socket->state != state_ACTIVE)) {
					socket = socket->next;
					continue;
				}

				if ((ci->state == state_AUTHD)
				    && (ci->real_user.username)
				    && (ci->real_user.resource)
				    && (wpj->cfg->auto_refresh_sessions)) {
					/* set status refresh */
					ci->state =
					    state_REFRESHING_SESSION;
				} else {
					/* close not authed connections */
					ci->state = state_CLOSING;
					if (socket->accepted) {
						io_write(socket, NULL,
							 "<stream:error>Server temporarily unavailable</stream:error></stream:stream>",
							 -1);
					}
					temp = socket;
					socket = socket->next;
					io_close(temp);
					usleep(10);
					close_client(temp);
					continue;
				}
				socket = socket->next;
			}
		}

		/* pause 2 seconds, and reconnect */
		log_debug("Reconnect to %s:%d. Retry in 2 seconds...",
			  si->mip, si->mport);
		io_connect(si->mip, si->mport, server_process_xml,
			   (void *) si, 2);
		return;
	}
}
