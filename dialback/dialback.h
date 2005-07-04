
#include <jabberd.h>

/* s2s instance */
typedef struct db_struct {
	instance i;
	HASHTABLE nscache;	/* host/ip local resolution cache */
	HASHTABLE out_connecting;	/* where unvalidated in-progress connections are, key is to/from */
	HASHTABLE out_ok_db;	/* hash table of all connected dialback hosts, key is same to/from */
	HASHTABLE in_id;	/* all the incoming connections waiting to be checked, rand id attrib is key */
	HASHTABLE in_ok_db;	/* all the incoming dialback connections that are ok, ID@to/from is key  */
	SEM_VAR nscache_sem;
	SEM_VAR out_sem;
	SEM_VAR in_sem;

	char *secret;		/* our dialback secret */
	int timeout_packets;
	int timeout_idle;
	int cache_idle_timeout;
	int cache_max_timeout;
	time_t now;
} *db, _db;


typedef struct db_dns_cache_struct {
	WPHASH_BUCKET;
	pool p;
	char ip[16];
	char *host;
//  time_t last;
//  time_t create;
} *db_dns_cache, _db_dns_cache;

/* wrap an mio and track the idle time of it */
typedef struct miod_struct {
	WPHASH_BUCKET;
	pool p;
	volatile mio m;
	char *server;
	db d;
	int last, count;
} *miod, _miod;

void dialback_out_packet(db d, xmlnode x, char *ip);
result dialback_out_beat_packets(void *arg);

void dialback_in_read(mio s, int flags, void *arg, xmlnode x);
void dialback_in_verify(db d, xmlnode x);


miod dialback_miod_new(db d, mio m, pool p1);
void dialback_miod_write(miod md, xmlnode x);
void dialback_miod_read(miod md, xmlnode x);

/* thread safe */
char *dialback_ip_get(db d, jid host, char *ip);
void dialback_ip_set(db d, jid host, char *ip);

/* utils */
char *dialback_randstr_pool(pool p);
char *dialback_merlin(pool p, char *secret, char *to, char *challenge);
