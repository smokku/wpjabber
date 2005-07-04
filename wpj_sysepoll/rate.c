#include "wpj.h"

extern wpjs wpj;

int MAX_RATE;
int CONN_TIME;
int COUNT_CONN;
int RATE_WSP;
int RATE_KOSZT;

#define HASH_SIZE 16229

typedef struct Trate_bucket {
	int rate;		/* last rate  */
	DWORD time;		/* last ime */
	int punish;		/* 1 - do not connect */
	unsigned int ip;	/* key */
	unsigned int ilosc;
	struct Trate_bucket *next;
} _rate_bucket, *rate_bucket;

typedef struct Trate {
	unsigned int size;
	unsigned int count;
	rate_bucket table;
	rate_bucket tab[HASH_SIZE + 1];
	rate_bucket first, last;	/* free elements,first poczatek,last koniec */
} _wpj_rate, *wpj_rate;

wpj_rate rate = NULL;


void RateInit(int conn, int time, int count)
{
	rate_bucket bucket;
	int i;

	MAX_RATE = conn * 100;
	CONN_TIME = time * 100;
	RATE_KOSZT = 100;
	COUNT_CONN = count;

	log_debug("MAX_RATE = %d TIME %d KOSZT %d",
		  MAX_RATE, CONN_TIME, RATE_KOSZT);

	rate = malloc(sizeof(_wpj_rate));
	rate->size = HASH_SIZE;
	rate->count = 0;
	rate->table = malloc(sizeof(_rate_bucket) * (HASH_SIZE + 1));
	rate->first = NULL;
	rate->last = NULL;
	log_debug("Rate initialized");

	for (i = 0; i <= HASH_SIZE; i++)
		rate->tab[i] = NULL;

	for (i = 0; i < HASH_SIZE; i++) {
		bucket = &(rate->table[i]);
		bucket->next = NULL;

		if (rate->last == NULL)	/* if no first element */
			rate->last = bucket;
		else
			rate->first->next = bucket;

		rate->first = bucket;
	}

}

void RateStop()
{
	free(rate->table);
	free(rate);
	rate = NULL;
	log_debug("Rate freed");
}


/* return 0 - can connect, 1 connection rated */
int RateCheckAdd(struct in_addr sin_addr)
{
	unsigned long ip;
	rate_bucket bucket;
	int rate_add;
	int h, newrate;
	DWORD time;
	struct Trate_bucket *temp = NULL;

	ip = sin_addr.s_addr;

	time = timeGetTimeSec100();	/* 1 sek = 100 * time */

	/* get backet */
	h = ip % rate->size;	/* pos in table */

	for (bucket = rate->tab[h]; bucket != NULL; bucket = bucket->next) {
		if (bucket->ip == ip) {
			break;
		}
		temp = bucket;
	}

	/* create new */
	if (bucket == NULL) {
		log_debug("create bucket and add %d %d", h, ip);

		/* get bucket from cache */
		bucket = rate->last;	/* get one */
		if (!bucket) {
			log_alert("fatal", "No more cache !");
			return 0;
		}
		rate->last = bucket->next;

		log_debug("get for cache %p", bucket);

		/* set data */
		bucket->ip = ip;
		bucket->ilosc = 1;
		bucket->rate = MAX_RATE - RATE_KOSZT;
		bucket->time = time;
		bucket->punish = 0;


		/* add to hash at the end */
		//    bucket->next = NULL;
		// if (temp)
		//    temp->next = bucket;
		//else
		//    rate->tab[h] = bucket;

		/* add to hash at the begining */
		bucket->next = rate->tab[h];
		rate->tab[h] = bucket;

		/* ++ */
		rate->count++;

		/* return not rated */
		return 0;
	}

	if (COUNT_CONN) {
		if (bucket->ilosc + 1 >= COUNT_CONN) {
			log_debug("max conns reached");
			return 1;
		}
	}

	/* check if can connect */
	/* it means that we have connect */
	log_debug("before: rate %d, time %d", bucket->rate, bucket->time);

	/* first add points */
	rate_add = (MAX_RATE * (time - bucket->time)) / CONN_TIME;
	newrate = min(MAX_RATE, bucket->rate + rate_add);

	log_debug("newrate %d, time %d rate_add %d", newrate, time,
		  rate_add);

	if (newrate < RATE_KOSZT) {	/* koszt polaczenia */
		log_debug("conn rated");
		return 1;
	}

	bucket->rate = newrate - RATE_KOSZT;
	bucket->time = time;

	log_debug("after rate %d", bucket->rate);

	/* add ref to bucket */
	bucket->ilosc++;
	return 0;
}


/* remove rate */
/* -1 - nothing removed */
/* 0  - removed and no more client form this  IP in hash */
/* >0 - number of clients from this IP in hash */
int RateRemove(struct in_addr sin_addr)
{
	unsigned long ip;
	rate_bucket bucket;
	int h;
	struct Trate_bucket *temp = NULL;

	ip = sin_addr.s_addr;

	/* get backet */
	h = ip % rate->size;	/* pos in table */
	for (bucket = rate->tab[h]; bucket != NULL; bucket = bucket->next) {
		if (bucket->ip == ip) {
			log_debug("add: found");
			break;
		}
		temp = bucket;
	}

	if (bucket == NULL)
		return -1;

	log_debug("count -- %d", bucket->ilosc);
	bucket->ilosc--;

	if (bucket->ilosc)
		return bucket->ilosc;

	/* remove */
	log_debug("remove last one");

	/* if not first element */
	if (temp)
		temp->next = bucket->next;
	else
		rate->tab[h] = bucket->next;


	/* add to cache */
	bucket->next = NULL;

	if (rate->last == NULL)	/* if no first element */
		rate->last = bucket;
	else
		rate->first->next = bucket;

	rate->first = bucket;

	rate->count--;

	log_debug("put to cache %p count %d", bucket, rate->count);

	return 0;
}
