
#include "dialback.h"

/* simple queue for out_queue */
typedef struct dboq_struct
{
    time_t stamp;
    xmlnode x;
    struct dboq_struct *next;
} *dboq, _dboq;

/* for connecting db sockets */
typedef struct
{
    WPHASH_BUCKET;
    char *ip;
    int stamp;
    db d;
    jid key;
    xmlnode verifies;
    pool p;
    dboq q;
    mio m;
} *dboc, _dboc;

void dialback_out_read(mio m, int flags, void *arg, xmlnode x);

/* try to start a connection based upon this connect object */
void dialback_out_connect(dboc c){
    char *ip, *col;
    int port = 5269;

    if(c->ip == NULL)
	return;

    ip = c->ip;
    c->ip = strchr(ip,',');
    if(c->ip != NULL){ /* chop off this ip if there is another, track the other */
	*c->ip = '\0';
	c->ip++;
    }

    log_debug("Attempting to connect to %s at %s",jid_full(c->key),ip);

    /* get the ip/port for io_select */
    col = strchr(ip,':');
    if(col != NULL){
	*col = '\0';
	col++;
	port = atoi(col);
    }
    mio_connect(ip, port, dialback_out_read, (void *)c, 20, MIO_CONNECT_XML);
}

/* new connection object, use this function under sem */
dboc dialback_out_connection(db d, jid key, char *ip){
    dboc c;
    pool p;

    if(ip == NULL)
	return NULL;

	/* if connecting just return */
    if((c = wpxhash_get(d->out_connecting, jid_full(key))) != NULL){
	return c;
	}

    /* none, make a new one */
    p = pool_heap(2*1024);
    c = pmalloco(p, sizeof(_dboc));
    c->p = p;
    c->d = d;
    c->key = jid_new(p,jid_full(key));
    c->stamp = time(NULL);
    c->verifies = xmlnode_new_tag_pool(p,"v");
    c->ip = pstrdup(p,ip);

    /* insert in the hash */
    wpxhash_put(d->out_connecting, pstrdup(p,jid_full(key)), (void *)c);

    /* start the connection process */
    dialback_out_connect(c);

    return c;
}

/* either we're connected, or failed, or something like that, but the connection process is kaput */
/* always in I/O thread */
void dialback_out_connection_cleanup(dboc c){
    dboq cur, next;
    xmlnode x;

    wpxhash_zap(c->d->out_connecting,jid_full(c->key));

    /* if there was never any ->m set but there's a queue yet, then we probably never got connected, just make a note of it */
    if(c->m == NULL && c->q != NULL){
	log_notice(c->key->server,"failed to establish connection");
	}

	SEM_UNLOCK(c->d->out_sem);

    /* if there's any packets in the queue, flush them! */
    cur = c->q;
    while(cur != NULL){
	next = cur->next;
	deliver_fail(dpacket_new(cur->x),"Server Connect Failed");
	cur = next;
	  }

    /* also kill any validations still waiting */
    for(x = xmlnode_get_firstchild(c->verifies); x != NULL; x = xmlnode_get_nextsibling(x)){
	  jutil_tofrom(x);
	  dialback_in_verify(c->d, xmlnode_dup(x)); /* it'll take these verifies and trash them */
	}

    pool_free(c->p);
}


/* any thread */
void dialback_out_packet(db d, xmlnode x, char *ip){
    jid to, from, key;
    miod md;
    int verify = 0;
    dboq q;
    dboc c;

    to = jid_new(xmlnode_pool(x),xmlnode_get_attrib(x,"to"));
    from = jid_new(xmlnode_pool(x),xmlnode_get_attrib(x,"from"));
    if(to == NULL || from == NULL){
	log_warn(d->i->id, "dropping packet, invalid to or from: %s", xmlnode2str(x));
	xmlnode_free(x);
	return;
    }

    log_debug("dbout packet[%s]",xmlnode2str(x));

    /* db:verify packets come in with us as the sender */
    if(j_strcmp(from->server, d->i->id) == 0){
	verify = 1;
	/* fix the headers, restore the real from */
	xmlnode_put_attrib(x,"from",xmlnode_get_attrib(x,"ofrom"));
	xmlnode_hide_attrib(x,"ofrom");
	from = jid_new(xmlnode_pool(x),xmlnode_get_attrib(x,"from"));

		/* check from */
		if(from == NULL){
		  log_warn(d->i->id, "dropping packet, invalid from: %s", xmlnode2str(x));
		  xmlnode_free(x);
		  return;
		}
    }

    /* build the standard key
	 to/from
    */
    key = jid_new(xmlnode_pool(x),to->server);
    jid_set(key, from->server, JID_RESOURCE);

    /* try to get an active out connection */
	SEM_LOCK(d->out_sem);
    md = wpxhash_get(d->out_ok_db, jid_full(key));

    /* yay! that was easy, just send the packet :) */
    if(md != NULL){
	    log_debug("outgoing packet with key %s and located existing %X",jid_full(key),md);
	/* if we've got an ip sent, and a connected host, we should be registered! */
	if(ip != NULL)
	    register_instance(md->d->i, key->server);

	dialback_miod_write(md, x);
		SEM_UNLOCK(d->out_sem);
	return;
    }

	/* md = NULL */
    c = dialback_out_connection(d, key, dialback_ip_get(d, key, ip));

    if (!c){
      char *ipp;

      if (ip) ipp = ip; else ipp = "NONE";

      log_warn(key->server,"Unable to connect packet: %s  ip %s ",
		   xmlnode2str(x), ipp);
    }

    /* verify requests can't be queued, they need to be sent outright */
    if(verify){
	if(c == NULL){

	    SEM_UNLOCK(d->out_sem);
	    jutil_tofrom(x); /* pretend it bounced */
	    dialback_in_verify(d, x); /* no connection to send db:verify to, bounce back to in to send failure */
	    return;
	}

	/* if the server is already connected, just write it */
	if (c->m != NULL){
		  mio_write(c->m, x, NULL, -1);
	}else{	/* queue it so that it's written after we're connected */
		  xmlnode_insert_tag_node(c->verifies,x);
		  xmlnode_free(x);
	}

		SEM_UNLOCK(d->out_sem);
	return;
    }

    if(c == NULL){
	    SEM_UNLOCK(d->out_sem);
	log_warn(d->i->id,"dropping a packet that was missing an ip to connect to: %s",xmlnode2str(x));
	xmlnode_free(x);
	return;
    }

    /* insert into the queue */
    q = pmalloco(xmlnode_pool(x), sizeof(_dboq));
    q->stamp = time(NULL);
    q->x = x;
    q->next = c->q;
    c->q = q;
	SEM_UNLOCK(d->out_sem);
}


/* handle the events on an outgoing dialback socket, which isn't much of a job */
void dialback_out_read_db(mio m, int flags, void *arg, xmlnode x){
    miod md = (miod)arg;
    db d;

    switch(flags){
    case MIO_XML_NODE:
    /* it's either a valid verify response, or bust! */
	  if(j_strcmp(xmlnode_get_name(x),"db:verify") == 0){
		  dialback_in_verify(md->d, x);
		  return;
		}

	  if(j_strcmp(xmlnode_get_name(x),"stream:error") == 0){
		  log_debug("reveived stream error: %s",xmlnode_get_data(x));
		}else{
		  mio_write(m, NULL, "<stream:error>Not Allowed to send data on this socket!</stream:error>", -1);
		}

	  mio_close(m);
	  xmlnode_free(x);
	  return;

    case MIO_CLOSED:
	  d = md->d;

	  SEM_LOCK(d->out_sem);
	  unregister_instance(d->i, md->server);

	  /* save ip for logs */
	  log_record(md->server, "out", "dialback", "%d %s", md->count, md->m->ip);

	  wpxhash_zap(d->out_ok_db,md->wpkey);
	  pool_free(md->p);

	  SEM_UNLOCK(d->out_sem);
	  return;

	default:
	  return;
	}
}


/* util to flush queue to mio */
void dialback_out_qflush(miod md, dboq q){
    dboq cur, next;

    cur = q;
    while(cur != NULL){
	next = cur->next;
	dialback_miod_write(md, cur->x);
	cur = next;
    }
}

/* handle the early connection process */
/* I/O thread and connection thread for MIO_NEW */
void dialback_out_read(mio m, int flags, void *arg, xmlnode x){
    dboc c = (dboc)arg;
    xmlnode cur;
    miod md;

    log_debug("dbout read: fd %d flag %d key %s",m->fd, flags, jid_full(c->key));

    switch(flags){
    case MIO_NEW:
	log_debug("NEW outgoing server socket connected at %d",m->fd);

	/* outgoing connection, write the header */
	cur = xstream_header("jabber:server", c->key->server, NULL);
	xmlnode_put_attrib(cur,"xmlns:db","jabber:server:dialback"); /* flag ourselves as dialback capable */
	mio_write(m, NULL, xstream_header_char(cur), -1);
	xmlnode_free(cur);
	return;

    case MIO_XML_ROOT:
	log_debug("Incoming root %s",xmlnode2str(x));
	/* validate namespace */
	if(j_strcmp(xmlnode_get_attrib(x,"xmlns"),"jabber:server") != 0){
	    mio_write(m, NULL, "<stream:error>Invalid Stream Header!</stream:error>", -1);
	    mio_close(m);
	    break;
	}

	/* make sure we're not connecting to ourselves */
		SEM_LOCK(c->d->in_sem);
	if(wpxhash_get(c->d->in_id,xmlnode_get_attrib(x,"id")) != NULL){
		    SEM_UNLOCK(c->d->in_sem);
	    log_alert(c->key->server,"hostname maps back to ourselves!");
	    mio_write(m, NULL, "<stream:error>Mirror Mirror on the wall</stream:error>", -1);
	    mio_close(m);
	    break;
	}
		SEM_UNLOCK(c->d->in_sem);

	/* create and send our result request to initiate dialback */
	cur = xmlnode_new_tag("db:result");
	xmlnode_put_attrib(cur, "to", c->key->server);
	xmlnode_put_attrib(cur, "from", c->key->resource);
	xmlnode_insert_cdata(cur,
							 dialback_merlin(xmlnode_pool(cur), c->d->secret, c->key->server, xmlnode_get_attrib(x,"id")), -1);
	mio_write(m,cur, NULL, 0);

	/* well, we're connected to a dialback server, we can at least send verify requests now */

		/* lock verifies */
		SEM_LOCK(c->d->out_sem);

	c->m = m;
	for(cur = xmlnode_get_firstchild(c->verifies); cur != NULL; cur = xmlnode_get_nextsibling(cur)){
	    mio_write(m, xmlnode_dup(cur), NULL, -1);
	    xmlnode_hide(cur);
	}
		SEM_UNLOCK(c->d->out_sem);

	break;
    case MIO_XML_NODE:
	/* watch for a valid result, then we're set to rock! */
	if(j_strcmp(xmlnode_get_name(x),"db:result") == 0){
	    if(j_strcmp(xmlnode_get_attrib(x,"from"),c->key->server) != 0 || j_strcmp(xmlnode_get_attrib(x,"to"),c->key->resource) != 0){ /* naughty... *click* */
		log_warn(c->d->i->id,"Received illegal dialback validation remote %s != %s or to %s != %s",c->key->server,xmlnode_get_attrib(x,"from"),c->key->resource,xmlnode_get_attrib(x,"to"));
		mio_write(m, NULL, "<stream:error>Invalid Dialback Result</stream:error>", -1);
		mio_close(m);
		break;
	    }

	    /* process the returned result */
	    if(j_strcmp(xmlnode_get_attrib(x,"type"),"valid") == 0){
			    SEM_LOCK(c->d->out_sem);

		md = dialback_miod_new(c->d, m, NULL); /* set up the mio wrapper */
				log_debug("miod registering out connection: socket %d key %s",
						  md->m->fd, jid_full(c->key));
				md->server = pstrdup(md->p, c->key->server);

				wpxhash_put(c->d->out_ok_db, pstrdup(md->p,jid_full(c->key)), md);

				/* save ip */
				dialback_ip_set(md->d, c->key, md->m->ip);

				/* register us */
				register_instance(md->d->i, md->server);

				/* set socket */
		mio_reset(m, dialback_out_read_db, (void *)md);

		/* flush the queue of packets */
		dialback_out_qflush(md, c->q);
		c->q = NULL;

		/* we are connected, and can trash this now */
				/* free c */
		dialback_out_connection_cleanup(c);
		break;
	    }
	    /* something went wrong, we were invalid? */
	    log_alert(c->d->i->id,"We were told by %s that our sending name %s is invalid, either something went wrong on their end, we tried using that name improperly, or dns does not resolve to us",c->key->server,c->key->resource);
	    mio_write(m, NULL, "<stream:error>I guess we're trying to use the wrong name, sorry</stream:error>", -1);
	    mio_close(m);
	    break;
	}

	/* otherwise it's either a verify response, or bust! */
	if(j_strcmp(xmlnode_get_name(x),"db:verify") == 0){
	    dialback_in_verify(c->d, x);
	    return;
	}

	log_warn(c->d->i->id,"Dropping connection due to illegal incoming packet on an unverified socket from %s to %s (%s): %s",c->key->resource,c->key->server,m->ip,xmlnode2str(x));
	mio_write(m, NULL, "<stream:error>Not Allowed to send data on this socket!</stream:error>", -1);
	mio_close(m);
	break;

    case MIO_CLOSED:
	    SEM_LOCK(c->d->out_sem);
		c->m = NULL;
	if(c->ip == NULL)
		  dialback_out_connection_cleanup(c); /* buh bye! */
	else{
		  dialback_out_connect(c); /* this one failed, try another */
		  SEM_UNLOCK(c->d->out_sem);
		}
	return;

    default:
	return;
    }
    xmlnode_free(x);
}

void __dialback_deliver_fail(void *arg){
  xmlnode x = (xmlnode)arg;
  deliver_fail(dpacket_new(x),"Server Connect Timeout");
}

/* callback for walking the connecting hash tree */
int _dialback_out_beat_packets(void *arg, void *key, void *data){
    dboc c = (dboc)data;
	dboq cur, next, last;
	time_t now = (time_t)arg;

    /* time out individual queue'd packets */
	last = NULL;
    cur = c->q;
    while(cur != NULL){
	if((now - cur->stamp) <= c->d->timeout_packets){
	    last = cur;
	    cur = cur->next;
	    continue;
	}

	/* timed out sukkah! */
	next = cur->next;
	if(c->q == cur)
	    c->q = next;
	else
	    last->next = next;

		mtq_send(NULL, NULL, __dialback_deliver_fail,(void *) cur->x);
		//	  deliver_fail(dpacket_new(cur->x),"Server Connect Timeout");
	cur = next;
    }

    return 1;
}

result dialback_out_beat_packets(void *arg){
    db d = (db)arg;
	SEM_LOCK(d->out_sem);
    wpghash_walk(d->out_connecting,_dialback_out_beat_packets,(void*) time(NULL));
	SEM_UNLOCK(d->out_sem);
    return r_DONE;
}
