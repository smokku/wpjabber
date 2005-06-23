
/*
   Thread safe dialback module
   Modified by £ukasz Karwacki
*/


#include "dialback.h"

/* we need a decently random string in a few places */
char *dialback_randstr_pool(pool p){
    char *ret;

	ret = pmalloco(p,41);
    snprintf(ret,40,"%d",rand());
    shahash_r(ret,ret);
    return ret;
}

/* convenience */
char *dialback_merlin(pool p, char *secret, char *to, char *challenge){
    static char res[41];

    shahash_r(secret,			    res);
    shahash_r(spools(p, res, to, p),	    res);
    shahash_r(spools(p, res, challenge, p), res);

    log_debug("merlin casts his spell(%s+%s+%s) %s",secret,to,challenge,res);
    return res;
}

void dialback_miod_write(miod md, xmlnode x){
    md->count++;
    md->last = time(NULL);
    mio_write(md->m, x, NULL, -1);
}

void dialback_miod_read(miod md, xmlnode x){
    jpacket jp = jpacket_new(x);

    /* only accept valid jpackets! */
    if(jp == NULL){
	log_warn(md->d->i->id, "dropping invalid packet from server: %s",xmlnode2str(x));
	xmlnode_free(x);
	return;
    }

    /* send it on! */
    md->count++;
    md->last = time(NULL);
    deliver(dpacket_new(x),md->d->i);
}

miod dialback_miod_new(db d, mio m, pool p1){
    miod md;
	pool p;

	if (p1 == NULL)
	  p = pool_heap(512);
	else
	  p = p1;

    md = pmalloco(p, sizeof(_miod));
	md->p = p;
    md->m = m;
    md->d = d;
    md->last = time(NULL);

    return md;
}

/* IP caching */
char *dialback_ip_get(db d, jid host, char *ip){
    char *ret;
	db_dns_cache cache;

    if(host == NULL)
	return NULL;

    if(ip != NULL)
	return ip;

	SEM_LOCK(d->nscache_sem);
	cache = (db_dns_cache)wpxhash_get(d->nscache,host->server);
	if (cache == NULL){
	  SEM_UNLOCK(d->nscache_sem);
	  return NULL;
	}
	ret = pstrdup(host->p,cache->ip);
	SEM_UNLOCK(d->nscache_sem);

    log_debug("returning cached ip %s for %s",ret,host->server);

    return ret;
}

void dialback_ip_set(db d, jid host, char *ip){
  db_dns_cache cache;
  pool p;

  if(host == NULL || ip == NULL)
	return;

  SEM_LOCK(d->nscache_sem);
  cache = (db_dns_cache)wpxhash_get(d->nscache,host->server);

  if (cache != NULL){
    /* new cache */
    if (strcmp(cache->ip, ip)){
      strncpy(cache->ip,ip,16);
    }
//    cache->last = time(NULL);
    SEM_UNLOCK(d->nscache_sem);
    return;
  }

  p = pool_heap(512);
  cache = pmalloco(p,sizeof(_db_dns_cache));
  cache->p = p;
  cache->host = pstrdup(cache->p,host->server);
  strncpy(cache->ip,ip,16);
//  cache->last = cache->create = time(NULL);

  wpxhash_put(d->nscache, cache->host, (void *)cache);

  SEM_UNLOCK(d->nscache_sem);

  log_debug("cached ip %s for %s",ip,host->server);
}

/*
void _dialback_beat_cache(wpxht h, const char *key, void *val, void *arg){
    db_dns_cache cache = (db_dns_cache) val;
    db d = (db)arg;

    if((d->now - cache->last) >= d->cache_idle_timeout){
      wpxhash_zap(h,key);
      pool_free(cache->p);
      return;
    }
    if((d->now - cache->last) >= d->cache_max_timeout){
      wpxhash_zap(h,key);
      pool_free(cache->p);
      return;
    }
}

result dialback_beat_cache(void *arg){
    db d = (db)arg;

    log_debug("dialback cache check");

    d->now = time(NULL);

    SEM_LOCK(d->nscache_sem);
    wpxhash_walk(d->nscache, _dialback_beat_cache,(void*)d);
    SEM_UNLOCK(d->nscache_sem);

    return r_DONE;
}
*/

/* phandler callback, send packets to another server */
result dialback_packets(instance i, dpacket dp, void *arg){
    db d = (db)arg;
    xmlnode x = dp->x;
    char *ip = NULL;

    /* routes are from dnsrv w/ the needed ip */
    if(dp->type == p_ROUTE){
	x = xmlnode_get_firstchild(x);
	ip = xmlnode_get_attrib(dp->x,"ip");
    }

    /* all packets going to our "id" go to the incoming handler,
     * it uses that id to send out db:verifies to other servers,
     * and end up here when they bounce */
    if(j_strcmp(xmlnode_get_attrib(x,"to"),d->i->id) == 0){
	xmlnode_put_attrib(x,"to",xmlnode_get_attrib(x,"ofrom"));
	xmlnode_hide_attrib(x,"ofrom"); /* repair the addresses */
	dialback_in_verify(d, x);
	return r_DONE;
    }

    dialback_out_packet(d, x, ip);
    return r_DONE;
}


/* callback for walking each miod-value host hash tree */
/* heartbeat thread */
void _dialback_beat_idle(wpxht h, char *key, void *val, void *arg){
    miod md = (miod)val;

    if(((int)*(time_t*)arg - md->last) >= md->d->timeout_idle){
	log_debug("Idle Timeout on socket %d to %s",md->m->fd, md->m->ip);
	mio_close(md->m);
    }
}

/* heartbeat checker for timed out idle hosts */
result dialback_beat_idle(void *arg){
    db d = (db)arg;
    time_t ttmp;

    log_debug("dialback idle check");
    time(&ttmp);

	SEM_LOCK(d->out_sem);
    wpxhash_walk(d->out_ok_db, _dialback_beat_idle,(void*)&ttmp);
	SEM_UNLOCK(d->out_sem);

	SEM_LOCK(d->in_sem);
    wpxhash_walk(d->in_ok_db, _dialback_beat_idle,(void*)&ttmp);
	SEM_UNLOCK(d->in_sem);

    return r_DONE;
}

/*** everything starts here ***/
void dialback(instance i, xmlnode x){
    db d;
    xmlnode cfg, cur;
    struct karma k;
    int max;
    int rate_time, rate_points;
    int set_rate = 0, set_karma=0;

    log_debug("dialback loading");
    srand(time(NULL));

    /* get the config */
    cfg = xdb_get(xdb_cache(i),jid_new(xmlnode_pool(x),"config@-internal"),"jabber:config:dialback");

    max = j_atoi(xmlnode_get_tag_data(cfg,"maxhosts"),997);

    d = pmalloco(i->p,sizeof(_db));

    d->nscache = wpxhash_new(max);
    d->out_connecting = wpxhash_new(67);
    d->out_ok_db = wpxhash_new(max);
    d->in_id = wpxhash_new(max);
    d->in_ok_db = wpxhash_new(max);

    SEM_INIT(d->nscache_sem);
    SEM_INIT(d->out_sem);
    SEM_INIT(d->in_sem);

    d->i = i;

    d->timeout_idle = j_atoi(xmlnode_get_tag_data(cfg,"idletimeout"),900);
    d->timeout_packets = j_atoi(xmlnode_get_tag_data(cfg,"queuetimeout"),30);

    log_alert("config","Idle timeout: %d, packet send timeout: %d",
			  d->timeout_idle,d->timeout_packets);

    d->cache_idle_timeout =
      j_atoi(xmlnode_get_tag_data(cfg,"cacheidletimeout"),3600);
    d->cache_max_timeout =
      j_atoi(xmlnode_get_tag_data(cfg,"cachemaxtimeout"),3600*24*7);

    d->secret = xmlnode_get_attrib(cfg,"secret");

    if(d->secret == NULL) /* if there's no configured secret, make one on the fly */
	  d->secret = dialback_randstr_pool(i->p);

    /* Get rate info if it exists */
	rate_time = 0;
	rate_points = 0;
    if((cur = xmlnode_get_tag(cfg, "rate")) != NULL){
	rate_time   = j_atoi(xmlnode_get_attrib(cur, "time"), 0);
	rate_points = j_atoi(xmlnode_get_attrib(cur, "points"), 0);
	set_rate = 1; /* set to true */
    }

    /* Get karma info if it exists */
    if((cur = xmlnode_get_tag(cfg, "karma")) != NULL){
	 k.val	       = j_atoi(xmlnode_get_tag_data(cur, "init"), KARMA_INIT);
	 k.max	       = j_atoi(xmlnode_get_tag_data(cur, "max"), KARMA_MAX);
	 k.inc	       = j_atoi(xmlnode_get_tag_data(cur, "inc"), KARMA_INC);
	 k.dec	       = j_atoi(xmlnode_get_tag_data(cur, "dec"), KARMA_DEC);
	 k.restore     = j_atoi(xmlnode_get_tag_data(cur, "restore"), KARMA_RESTORE);
	 k.penalty     = j_atoi(xmlnode_get_tag_data(cur, "penalty"), KARMA_PENALTY);
	 k.reset_meter = j_atoi(xmlnode_get_tag_data(cur, "resetmeter"), KARMA_RESETMETER);
	 set_karma = 1; /* set to true */
    }

    if((cur = xmlnode_get_tag(cfg,"ip")) != NULL)
	for(;cur != NULL; xmlnode_hide(cur), cur = xmlnode_get_tag(cfg,"ip")){
	    mio m;
	    m = mio_listen(j_atoi(xmlnode_get_attrib(cur,"port"),5269),xmlnode_get_data(cur),dialback_in_read,(void*)d, MIO_LISTEN_XML);
	    if(m == NULL)
		return;
	    /* Set New rate and points */
	    if(set_rate == 1) mio_rate(m, rate_time, rate_points);
	    /* Set New karma values */
	    if(set_karma == 1) mio_karma2(m, &k);
	}
    else /* no special config, use defaults */
    {
	mio m;
	m = mio_listen(5269,NULL,dialback_in_read,(void*)d, MIO_LISTEN_XML);
	if(m == NULL) return;
	/* Set New rate and points */
	if(set_rate == 1) mio_rate(m, rate_time, rate_points);
	/* Set New karma values */
	if(set_karma == 1) mio_karma2(m, &k);
    }

    register_phandler(i,o_DELIVER,dialback_packets,(void*)d);

    register_beat(d->timeout_idle, dialback_beat_idle, (void *)d);
    register_beat(d->timeout_packets, dialback_out_beat_packets, (void *)d);


//  register_beat(600, dialback_beat_cache, (void *)d);

    xmlnode_free(cfg);
}









