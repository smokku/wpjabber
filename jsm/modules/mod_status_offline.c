/*
  Author Lukas Karwacki

Add to config file to enable offline status
  <mod_status_offline>
  </mod_status_offline>


  TODO:
  - invisible
  - communication blocking

*/

#include "jsm.h"

#ifndef WPJABBER
#define SEM_INIT(arg...)
#define SEM_LOCK(arg...)
#define SEM_UNLOCK(arg...)
#define SEM_VAR int
#define WPHASH_BUCKET
#define wpxhash_get xhash_get
#define wpxhash_put xhash_put
#define wpxhash_zap xhash_zap
#define wpxhash_walk xhash_walk
#define wpxhash_new_pool(x,y) xhash_new(x)
#define wpxht xht
#endif

//#define TEST

/** 30 days */
#ifdef TEST
/** clear hash every 7 days */
#define HASH_CLEAR_TIMEOUT	 20
/** realloc hash every 24 hours */
#define HASH_REALLOC_TIMEOUT	 10
#define DEFAULT_STATUS_HASH_SIZE 13
#define OFFLINE_STATUS_TIMEOUT	 60
#define CLEAR_STATUS_HASH_SIZE	 1
#define BEAT_OFFLINE_STATUS	 10
#else
/** clear hash every 7 days */
#define HASH_CLEAR_TIMEOUT	 3600*24*7
/** realloc hash every 24 hours */
#define HASH_REALLOC_TIMEOUT	 3600*24
#define DEFAULT_STATUS_HASH_SIZE 13523
#define OFFLINE_STATUS_TIMEOUT	 3600*24*30
#define CLEAR_STATUS_HASH_SIZE	 20000
#define BEAT_OFFLINE_STATUS	 3600
#endif

#define MAX_STATUS_LEN 256

typedef struct status_bucket_struct {
	WPHASH_BUCKET;
	pool p;
	char *status;
	time_t last;
} _status_bucket, *status_bucket;

typedef struct status_offline_struct {
	time_t now;
	int count;
	HASHTABLE hash;
	SEM_VAR sem;
	time_t last_hash_clear, last_hash_realloc;
} _status_off, *status_off;

/** session out
	User set its offline presence
*/
mreturn mod_status_offline_out(mapi m, void *arg)
{
	status_off so = (status_off) arg;
	char *status;
	status_bucket sb, sbnew;
	pool p;

	if (m->packet->type != JPACKET_PRESENCE)
		return M_IGNORE;

	/* if packet to or not unavailable */
	if ((m->packet->to != NULL ||
	     (jpacket_subtype(m->packet) != JPACKET__UNAVAILABLE)))
		return M_PASS;

	status = xmlnode_get_tag_data(m->packet->x, "status");
	/* set new */

	SEM_LOCK(so->sem);

	if (status) {

		sb = (status_bucket) wpxhash_get(so->hash, m->user->user);

		if (strlen(status) > MAX_STATUS_LEN) {
			*(status + MAX_STATUS_LEN) = 0;
		}
		p = pool_heap(strlen(status) + strlen(m->user->user) +
			      sizeof(_status_bucket) + 10);
		sbnew = pmalloco(p, sizeof(_status_bucket));
		sbnew->p = p;
		sbnew->status = pstrdup(p, status);
		sbnew->last = time(NULL);

		log_debug("Set new status %s for user %s", status,
			  m->user->user);

		wpxhash_put(so->hash, pstrdup(p, m->user->user),
			    (void *) sbnew);

		if (sb) {
			log_debug("Remove old status");
			pool_free(sb->p);
		} else {
			so->count++;
		}
	} else {
		sb = (status_bucket) wpxhash_get(so->hash, m->user->user);
		if (sb) {
			log_debug("Remove status for user %s",
				  m->user->user);
			wpxhash_zap(so->hash, m->user->user);
			so->count--;
			pool_free(sb->p);
		}
	}

	SEM_UNLOCK(so->sem);

	return M_PASS;
}

mreturn mod_status_offline_session(mapi m, void *arg)
{
	js_mapi_session(es_OUT, m->s, mod_status_offline_out, arg);
  /** XXX handle es_IN to handle status when invisible */
	return M_PASS;
}

mreturn mod_status_offline_offline(mapi m, void *arg)
{
	status_off so = (status_off) arg;
	status_bucket sb;
	xmlnode x;

	if (m->packet->type != JPACKET_PRESENCE)
		return M_IGNORE;

	if (jpacket_subtype(m->packet) != JPACKET__PROBE)
		return M_PASS;

	/* if user has sessions */
	if (m->user->sessions) {
		/* not often user has sessions */
		session cur;
		int inv = 0;

		SEM_LOCK(m->user->sem);

		for (cur = m->user->sessions; cur != NULL; cur = cur->next) {
			if (cur->priority < 0)
				continue;

			if (j_strcmp
			    (xmlnode_get_attrib(cur->presence, "type"),
			     "unavailable") != 0) {
				/* if any available session, PASS */
				inv = 1;
				break;
			}
			/* if priority >= 0 and unavailable presence -> invisible */
		}
		SEM_UNLOCK(m->user->sem);

		if (inv)
			return M_PASS;
	}

	SEM_LOCK(so->sem);
	sb = (status_bucket) wpxhash_get(so->hash, m->user->user);
	if (sb) {
		xmlnode delay;
		struct tm new_time;
		char stamp[18];

		x = jutil_presnew(JPACKET__UNAVAILABLE,
				  jid_full(m->packet->from), sb->status);

		delay = xmlnode_insert_tag(x, "x");
		xmlnode_put_attrib(delay, "xmlns", NS_DELAY);

		localtime_r(&(sb->last), &new_time);
		snprintf(stamp, 18, "%d%02d%02dT%02d:%02d:%02d",
			 1900 + new_time.tm_year, new_time.tm_mon + 1,
			 new_time.tm_mday, new_time.tm_hour,
			 new_time.tm_min, new_time.tm_sec);

		xmlnode_put_attrib(delay, "stamp", stamp);

		SEM_UNLOCK(so->sem);
		xmlnode_put_attrib(x, "from", jid_full(m->packet->to));

		/* XXX postout offline */
		js_deliver(m->si, jpacket_new(x));
	} else {
		SEM_UNLOCK(so->sem);
	}

	return M_PASS;
}

void _del_offline_status(wpxht h, char *key, void *val, void *arg)
{
	status_off so = (status_off) arg;
	status_bucket sb = (status_bucket) val;

	if (sb->last + OFFLINE_STATUS_TIMEOUT < so->now) {
		log_debug("remove timeouted status for user %s", key);

		wpxhash_zap(h, key);
		pool_free(sb->p);
		so->count--;
	}
}

void _realloc_offline_status(wpxht h, char *key, void *val, void *arg)
{
	status_off so = (status_off) arg;
	status_bucket sb = (status_bucket) val;
	status_bucket sbnew;
	pool p;

	/* alloc new */
	p = pool_heap(strlen(sb->status) +
		      strlen(key) + sizeof(_status_bucket) + 10);
	sbnew = pmalloco(p, sizeof(_status_bucket));
	sbnew->p = p;
	sbnew->status = pstrdup(p, sb->status);
	sbnew->last = sb->last;
	/* replace */
	wpxhash_put(so->hash, pstrdup(p, key), (void *) sbnew);
	/* free old */
	pool_free(sb->p);
}

result mod_status_check(void *arg)
{
	status_off so = (status_off) arg;
	so->now = time(NULL);

	/* if hash is big or hash clear timeout */
	if ((so->count > CLEAR_STATUS_HASH_SIZE) ||
	    (so->last_hash_clear + HASH_CLEAR_TIMEOUT < so->now)) {

		so->last_hash_clear = so->now;

		log_alert("offline_status", "Hash clear. Count %d",
			  so->count);

		SEM_LOCK(so->sem);
		wpxhash_walk(so->hash, _del_offline_status, (void *) so);
		SEM_UNLOCK(so->sem);
	}

	if (so->last_hash_realloc + HASH_REALLOC_TIMEOUT < so->now) {
		so->last_hash_realloc = so->now;

		log_alert("offline_status", "Realloc");
		SEM_LOCK(so->sem);
		wpxhash_walk(so->hash, _realloc_offline_status,
			     (void *) so);
		SEM_UNLOCK(so->sem);
	}

	log_alert("offline_status", "Beat. Count %d", so->count);

	return r_DONE;
}

/** shutdown */
mreturn mod_status_offline_shutdown(mapi m, void *arg)
{
  /** store hash */
	return M_PASS;
}

void mod_status_offline(jsmi si)
{
	log_debug("init");

	/* read config */
	if (xmlnode_get_tag(si->config, "mod_status_offline")) {
		status_off so;

		so = pmalloco(si->p, sizeof(_status_off));

		log_alert("mod_status_offline", "ON");

		so->hash =
		    wpxhash_new_pool(DEFAULT_STATUS_HASH_SIZE, si->p);
		SEM_INIT(so->sem);
		so->count = 0;
		so->last_hash_clear = time(NULL);
		so->last_hash_realloc = time(NULL);

		js_mapi_register(si, e_SHUTDOWN,
				 mod_status_offline_shutdown, (void *) so);
		js_mapi_register(si, e_SESSION, mod_status_offline_session,
				 (void *) so);
		js_mapi_register(si, e_OFFLINE, mod_status_offline_offline,
				 (void *) so);
		register_beat(BEAT_OFFLINE_STATUS, mod_status_check,
			      (void *) so);
	}
}
