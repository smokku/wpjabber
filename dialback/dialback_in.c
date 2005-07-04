#include "dialback.h"

/* db in connections */
typedef struct dbic_struct {
	WPHASH_BUCKET;
	pool p;
	volatile mio m;
	char *id;
	miod md;
	xmlnode results;	/* contains all pending results we've sent out */
	db d;
} *dbic, _dbic;

/* nice new dbic, I/O thread */
dbic dialback_in_dbic_new(db d, mio m)
{
	dbic c;
	pool p;

	p = pool_heap(512);
	c = pmalloco(p, sizeof(_dbic));
	c->p = p;
	c->m = m;
	c->md = NULL;
	c->id = dialback_randstr_pool(p);
	c->results = xmlnode_new_tag_pool(p, "r");
	c->d = d;

	log_debug("created incoming connection %s from %s", c->id, m->ip);

	SEM_LOCK(d->in_sem);
	wpxhash_put(d->in_id, c->id, (void *) c);
	SEM_UNLOCK(d->in_sem);
	return c;
}

/* callback for mio for accepted sockets that are dialback */
/* I/O thread */
void dialback_in_read_db(mio m, int flags, void *arg, xmlnode x)
{
	dbic c = (dbic) arg;
	miod md;
	jid key, from;
	xmlnode x2;

	switch (flags) {
	case MIO_XML_NODE:
		log_debug("dbin read dialback: fd %d packet %s", m->fd,
			  xmlnode2str(x));

		/* incoming verification request, check and respond */
		if (j_strcmp(xmlnode_get_name(x), "db:verify") == 0) {
			if (j_strcmp
			    (xmlnode_get_data(x),
			     dialback_merlin(xmlnode_pool(x), c->d->secret,
					     xmlnode_get_attrib(x, "from"),
					     xmlnode_get_attrib(x,
								"id"))) ==
			    0)
				xmlnode_put_attrib(x, "type", "valid");
			else
				xmlnode_put_attrib(x, "type", "invalid");

			/* reformat the packet reply */
			jutil_tofrom(x);
			while ((x2 = xmlnode_get_firstchild(x)) != NULL)
				xmlnode_hide(x2);	/* hide the contents */
			mio_write(m, x, NULL, 0);
			return;
		}

		/* valid sender/recipient jids */
		if ((from =
		     jid_new(xmlnode_pool(x),
			     xmlnode_get_attrib(x, "from"))) == NULL
		    || (key =
			jid_new(xmlnode_pool(x),
				xmlnode_get_attrib(x, "to"))) == NULL) {
			log_alert("error",
				  "Invalid packet recived from %s, packet %s, socket %p",
				  c->id, xmlnode2str(x), m);
			mio_write(m, NULL,
				  "<stream:error>Invalid Packets Received!</stream:error>",
				  -1);
			mio_close(m);
			xmlnode_free(x);
			return;
		}

		/* make our special key */
		jid_set(key, from->server, JID_RESOURCE);
		jid_set(key, c->id, JID_USER);	/* special user of the id attrib makes this key unique */

		/* incoming result, track it and forward on */
		if (j_strcmp(xmlnode_get_name(x), "db:result") == 0) {
			SEM_LOCK(c->d->in_sem);
			/* store the result in the connection, for later validation */
			xmlnode_put_attrib(xmlnode_insert_tag_node
					   (c->results, x), "key",
					   jid_full(key));

			SEM_UNLOCK(c->d->in_sem);
			/* send the verify back to them, on another outgoing trusted socket, via deliver (so it is real and goes through dnsrv and anything else) */
			x2 = xmlnode_new_tag_pool(xmlnode_pool(x),
						  "db:verify");
			xmlnode_put_attrib(x2, "to",
					   xmlnode_get_attrib(x, "from"));
			xmlnode_put_attrib(x2, "ofrom",
					   xmlnode_get_attrib(x, "to"));
			xmlnode_put_attrib(x2, "from", c->d->i->id);	/* so bounces come back to us to get tracked */
			xmlnode_put_attrib(x2, "id", c->id);
			xmlnode_insert_node(x2, xmlnode_get_firstchild(x));	/* copy in any children */

			deliver(dpacket_new(x2), c->d->i);

			return;
		}

		/* hmm, incoming packet on dialback line, there better be a valid entry for it or else! */
		SEM_LOCK(c->d->in_sem);
		md = wpxhash_get(c->d->in_ok_db, jid_full(key));
		if (md == NULL || md->m != m) {	/* dude, what's your problem!  *click* */
			SEM_UNLOCK(c->d->in_sem);
			if (md)
				log_alert("error",
					  " err md = %p, md->m = %p, m = %p",
					  md, md->m, m);
			else
				log_alert("error", " null md = %p, m = %p",
					  md, m);
			mio_write(m, NULL,
				  "<stream:error>Invalid Packets Recieved!</stream:error>",
				  -1);
			mio_close(m);
			xmlnode_free(x);
			return;
		}

		SEM_UNLOCK(c->d->in_sem);
		dialback_miod_read(md, x);
		return;

	case MIO_CLOSED:
		/* free md and dbic */
		SEM_LOCK(c->d->in_sem);
		c->m = NULL;
		wpxhash_zap(c->d->in_id, c->id);
		SEM_UNLOCK(c->d->in_sem);

		pool_free(c->p);
		return;

	default:
		return;
	}
}


/* callback for mio for accepted sockets */
/* IO thread */
void dialback_in_read(mio m, int flags, void *arg, xmlnode x)
{
	db d = (db) arg;
	xmlnode x2;
	//    char strid[10];
	dbic c;


	log_debug("dbin read: fd %d flag %d", m->fd, flags);

	if (flags != MIO_XML_ROOT)
		return;

	/* validate namespace */
	if (j_strcmp(xmlnode_get_attrib(x, "xmlns"), "jabber:server") != 0) {
		mio_write(m, NULL,
			  "<stream:stream><stream:error>Invalid Stream Header!</stream:error>",
			  -1);
		mio_close(m);
		xmlnode_free(x);
		return;
	}
	//    snprintf(strid, 9, "%X", m); /* for hashes for later */

	/* dialback connection, under sem */
	c = dialback_in_dbic_new(d, m);

	/* reset to a dialback packet reader */
	mio_reset(m, dialback_in_read_db, (void *) c);

	/* write our header */
	x2 = xstream_header("jabber:server", NULL,
			    xmlnode_get_attrib(x, "to"));
	xmlnode_put_attrib(x2, "xmlns:db", "jabber:server:dialback");	/* flag ourselves as dialback capable */
	xmlnode_put_attrib(x2, "id", c->id);	/* send random id as a challenge */
	mio_write(m, NULL, xstream_header_char(x2), -1);

	xmlnode_free(x2);
	xmlnode_free(x);
}

void miod_in_cleanup(void *arg)
{
	miod md = (miod) arg;

	log_record((char *) md->wpkey, "in", "dialback", "%d %s",
		   md->count, md->m->ip);

	SEM_LOCK(md->d->in_sem);
	wpxhash_zap(md->d->in_ok_db, md->wpkey);
	SEM_UNLOCK(md->d->in_sem);
}

/* we take in db:valid packets that have been processed, and expect the to="" to be our name and from="" to be the remote name */
/* any thread */
void dialback_in_verify(db d, xmlnode x)
{
	dbic c;
	xmlnode x2;
	jid key;
	miod md;

	log_debug("dbin validate: %s", xmlnode2str(x));

	/* check for the stored incoming connection first */
	SEM_LOCK(d->in_sem);
	if ((c =
	     wpxhash_get(d->in_id, xmlnode_get_attrib(x, "id"))) == NULL) {
		SEM_UNLOCK(d->in_sem);
		log_warn(d->i->id,
			 "dropping broken dialback validating request: %s",
			 xmlnode2str(x));
		xmlnode_free(x);
		return;
	}

	/* make a key of the sender/recipient addresses on the packet */
	key = jid_new(xmlnode_pool(x), xmlnode_get_attrib(x, "to"));
	jid_set(key, xmlnode_get_attrib(x, "from"), JID_RESOURCE);
	jid_set(key, c->id, JID_USER);	/* special user of the id attrib makes this key unique */

	if ((x2 =
	     xmlnode_get_tag(c->results,
			     spools(xmlnode_pool(x), "?key=",
				    jid_full(key),
				    xmlnode_pool(x)))) == NULL) {
		SEM_UNLOCK(d->in_sem);
		log_warn(d->i->id,
			 "dropping broken dialback validating request: %s",
			 xmlnode2str(x));
		xmlnode_free(x);
		return;
	}
	xmlnode_hide(x2);

	/* valid requests get the honour of being miod */
	if (j_strcmp(xmlnode_get_attrib(x, "type"), "valid") == 0) {
		md = dialback_miod_new(c->d, c->m, c->m->p);
		c->md = md;
		log_debug("miod in registering socket %d with key %s",
			  md->m->fd, jid_full(key));

		md->server = pstrdup(md->p, key->server);
		pool_cleanup(md->m->p, miod_in_cleanup, (void *) md);
		wpxhash_put(c->d->in_ok_db, pstrdup(md->p, jid_full(key)),
			    md);
	}

	/* rewrite and send on to the socket */
	x2 = xmlnode_new_tag_pool(xmlnode_pool(x), "db:result");
	xmlnode_put_attrib(x2, "to", xmlnode_get_attrib(x, "from"));
	xmlnode_put_attrib(x2, "from", xmlnode_get_attrib(x, "to"));
	xmlnode_put_attrib(x2, "type", xmlnode_get_attrib(x, "type"));
	mio_write(c->m, x2, NULL, -1);

	SEM_UNLOCK(d->in_sem);
}
