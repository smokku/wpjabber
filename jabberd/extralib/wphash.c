/* --------------------------------------------------------------------------
   WPHash

   Author: £ukasz Karwacki <lukasm@wp-sa.pl>

   Element putted into hash must have bucket definition at the begining.

   * --------------------------------------------------------------------------*/

#include "jabberd.h"

#define hasher_i_def(i) (unsigned int)((unsigned long)(i)%(unsigned long)h->prime)
#define hasher_def(s) hasher_i_def(_wpxhasher((s)))

/* Generates a hash code for a string.
 * This function uses the ZEND hashing algorithm
 * Copyright Zend Technologies Ltd. - v2.00 Zend License
 */
static inline unsigned long _wpxhasher(char *s){
	register unsigned char *name = (unsigned char *)s;
	register unsigned long h = 5381;

	while (*name){
		h += (h << 5);
		h ^= (unsigned long)(*name++);
	}
	return h;
}

/* init */
wpxht wpxhash_new(unsigned int prime){
	register wpxht xnew;

	xnew = (wpxht) malloc(sizeof(_wpxht) + sizeof(wpxhn)*prime);
	memset(xnew, 0, sizeof(_wpxht) + sizeof(wpxhn)*prime);
	xnew->prime = prime;
	xnew->zen = (wpxhn*)((unsigned char *)xnew + sizeof(_wpxht));
	return xnew;
}

wpxht wpxhash_new_pool(unsigned int prime, pool p){
	register wpxht xnew;

	xnew = pmalloco(p, sizeof(_wpxht) + sizeof(wpxhn)*prime);
	xnew->prime = prime;
	xnew->zen = (wpxhn*)((unsigned char *)xnew + sizeof(_wpxht));
	return xnew;
}

/*
  val must be wpxhn + other stuff
  wpkey will be updated


  put will replace !!!
  0 - error
  1 - put oki
  2 - replaced
*/
int wpxhash_put(wpxht h, char *key, void *val){
	wpxhn n,lastn;
	register unsigned int i;

	i = hasher_def(key);

	((wpxhn)val)->wpnext = NULL;

	if (h->zen[i] == NULL){
		h->zen[i] = (wpxhn)val;
		((wpxhn)val)->wpkey = key;
		h->count++;
		return 1;
	}

	lastn = NULL;
	for(n = h->zen[i]; n != NULL; n = n->wpnext){
		if(strcmp(key, n->wpkey) == 0)
			break;
		lastn = n;
	}

	/* if found n is founded, lastn is last element on n */
	if (n != NULL){
		/* replace old */

		/* if not first */
		if (lastn){
			lastn->wpnext = (wpxhn)val;
		} else{
			h->zen[i] = (wpxhn)val;
		}

		/* set data */
		((wpxhn)val)->wpnext = n->wpnext;

		((wpxhn)val)->wpkey = key;
		return 2;
	}

	lastn->wpnext = (wpxhn)val;
	((wpxhn)val)->wpkey = key;
	h->count++;

	return 1;
}


void *wpxhash_get(wpxht h, char *key){
	register wpxhn n;
	register unsigned int i;

	i = hasher_def(key);

	for(n = h->zen[i]; n != NULL; n = n->wpnext){
		if(j_strcmp(key, n->wpkey) == 0)
			return n;
	}
	return NULL;
}

char *wpxhash_get_value(wpxht h, char *key){
	register wpxhn n;
	register unsigned int i;

	i = hasher_def(key);

	for(n = h->zen[i]; n != NULL; n = n->wpnext){
		if(j_strcmp(key, n->wpkey) == 0)
			return ((wpxhn_char)n)->value;
	}
	return NULL;
}

void wpxhash_zap(wpxht h, char *key){
	wpxhn n,lastn;
	register unsigned int i;

	i = hasher_def(key);

	lastn = NULL;
	for(n = h->zen[i]; n != NULL; n = n->wpnext){
		if(strcmp(key, n->wpkey) == 0)
			break;
		lastn = n;
	}

	if (!n) return;

	/* if not first */
	if (lastn){
		lastn->wpnext = n->wpnext;
	} else{
		h->zen[i] = n->wpnext;
	}
	h->count--;
}

void wpxhash_free(wpxht h){
	if(h != NULL){
		free(h);
	}
}

void wpxhash_walk(wpxht h, wpxhash_walker w, void *arg){
	register unsigned int i;
	register wpxhn n,t;

	for(i = 0; i < h->prime; i++){
		for(n = h->zen[i]; n != NULL; ){
			t = n;
			n = t->wpnext;
			(*w)(h, t->wpkey, t, arg);
		}
	}
}

void wpghash_walk(wpxht h, wpghash_walker w, void *arg){
	register unsigned int i;
	register wpxhn n,t;

	for(i = 0; i < h->prime; i++){
		for(n = h->zen[i]; n != NULL; ){
			t = n;
			n = t->wpnext;
			(*w)(arg, t->wpkey, t);
		}
	}
}



/**************************************************************************/
/**************************************************************************/
/**************************************************************************/
/**************************************************************************/

wpxht_si wpxhash_si_new(unsigned int prime){
	register wpxht_si xnew;

	xnew = (wpxht_si) malloc(sizeof(_wpxht_si) + (sizeof(wpxhn_si)* prime * 2));
	memset(xnew, 0, sizeof(_wpxht_si) + (sizeof(wpxhn_si) * prime * 2));
	xnew->prime = prime;
	xnew->zen_str = (wpxhn_si*)((unsigned char *)xnew + sizeof(_wpxht_si));
	xnew->zen_int = (wpxhn_si*)((unsigned char *)xnew + sizeof(_wpxht_si) +
								(sizeof(wpxhn_si) * prime));

	return xnew;
}

wpxht_si wpxhash_si_new_pool(unsigned int prime, pool p){
	register wpxht_si xnew;

	xnew = (wpxht_si) pmalloco(p, sizeof(_wpxht_si) + (sizeof(wpxhn_si)* prime * 2));
	xnew->prime = prime;
	xnew->zen_str = (wpxhn_si*)((unsigned char *)xnew + sizeof(_wpxht_si));
	xnew->zen_int = (wpxhn_si*)((unsigned char *)xnew + sizeof(_wpxht_si) +
								(sizeof(wpxhn_si) * prime));

	return xnew;
}

/*
  val must be wpxhn + other stuff
  wpkey will be updated

  put will replace !!!
  0 - error
  1 - put oki
  2 - replaced
*/
int wpxhash_si_put(wpxht_si h, char *str_key, unsigned int int_key, void *val){
	register unsigned int index;
	register wpxhn_si n,lastn;

	index = int_key % h->prime;

	((wpxhn_si)val)->wpnext_str = NULL;
	((wpxhn_si)val)->wpnext_int = NULL;

	if (h->zen_int[index] == NULL){
		h->zen_int[index] = (wpxhn_si)val;
		((wpxhn_si)val)->wpkey_int = int_key;
		goto xput_str;
	}

	lastn = NULL;
	for(n = h->zen_int[index]; n != NULL; n = n->wpnext_int){
		if(n->wpkey_int == int_key)
			break;
		lastn = n;
	}

	/* if found n is founded, lastn is last element on n */
	if (n != NULL){
		/* if not first */
		if (lastn){
			lastn->wpnext_int = (wpxhn_si)val;
		} else{
			h->zen_int[index] = (wpxhn_si)val;
		}

		/* set data */
		((wpxhn_si)val)->wpnext_int = n->wpnext_int;

		((wpxhn_si)val)->wpkey_int = int_key;
		goto xput_str;
	}

	lastn->wpnext_int = (wpxhn_si)val;
	((wpxhn_si)val)->wpkey_int = int_key;

 xput_str:
	index = hasher_def(str_key);

	if (h->zen_str[index] == NULL){
		h->zen_str[index] = (wpxhn_si)val;
		((wpxhn_si)val)->wpkey_str = str_key;
		return 1;
	}

	lastn = NULL;
	for(n = h->zen_str[index]; n != NULL; n = n->wpnext_str){
		if(strcmp(str_key, n->wpkey_str) == 0)
			break;
		lastn = n;
	}

	/* if found n is founded, lastn is last element on n */
	if (n != NULL){
		/* replace old */

		/* if not first */
		if (lastn){
			lastn->wpnext_str = (wpxhn_si)val;
		} else{
			h->zen_str[index] = (wpxhn_si)val;
		}

		/* set data */
		((wpxhn_si)val)->wpnext_str = n->wpnext_str;

		((wpxhn_si)val)->wpkey_str = str_key;
		return 2;
	}

	lastn->wpnext_str = (wpxhn_si)val;
	((wpxhn_si)val)->wpkey_str = str_key;
	return 1;
}


void *wpxhash_si_get(wpxht_si h, char *str_key){
	register wpxhn_si n;
	register unsigned int i;

	i = hasher_def(str_key);

	for(n = h->zen_str[i]; n != NULL; n = n->wpnext_str){
		if(j_strcmp(str_key, n->wpkey_str) == 0)
			return n;
	}
	return NULL;
}

void *wpxhash_si_get_i(wpxht_si h, unsigned int int_key){
	register wpxhn_si n;
	register unsigned int i;

	i = int_key % h->prime;

	for(n = h->zen_int[i]; n != NULL; n = n->wpnext_int){
		if(int_key == n->wpkey_int)
			return n;
	}
	return NULL;
}

void wpxhash_si_zap(wpxht_si h, char *str_key){
	register wpxhn_si n,lastn;
	register unsigned int i;
	unsigned int int_key;

	i = hasher_def(str_key);

	lastn = NULL;
	for(n = h->zen_str[i]; n != NULL; n = n->wpnext_str){
		if(strcmp(str_key, n->wpkey_str) == 0)
			break;
		lastn = n;
	}

	if (!n) return;

	/* if not first */
	if (lastn){
		lastn->wpnext_str = n->wpnext_str;
	} else{
		h->zen_str[i] = n->wpnext_str;
	}

	/* INT */
	int_key = n->wpkey_int;

	i = int_key % h->prime;

	lastn = NULL;
	for(n = h->zen_int[i]; n != NULL; n = n->wpnext_int){
		if(int_key ==  n->wpkey_int)
			break;
		lastn = n;
	}

	if (!n){
		return;
	}

	/* if not first */
	if (lastn){
		lastn->wpnext_int = n->wpnext_int;
	} else{
		h->zen_int[i] = n->wpnext_int;
	}
}

void wpxhash_si_zap_i(wpxht_si h, unsigned int int_key){
	register wpxhn_si n,lastn;
	register unsigned int i;
	char *str_key;

	i = int_key % h->prime;

	lastn = NULL;
	for(n = h->zen_int[i]; n != NULL; n = n->wpnext_int){
		if(int_key ==  n->wpkey_int)
			break;
		lastn = n;
	}

	if (!n) return;

	/* if not first */
	if (lastn){
		lastn->wpnext_int = n->wpnext_int;
	} else{
		h->zen_int[i] = n->wpnext_int;
	}

	/* STR */
	str_key = n->wpkey_str;

	i = hasher_def(str_key);

	lastn = NULL;
	for(n = h->zen_str[i]; n != NULL; n = n->wpnext_str){
		if(strcmp(str_key, n->wpkey_str) == 0)
			break;
		lastn = n;
	}

	if (!n){
		return;
	}

	/* if not first */
	if (lastn){
		lastn->wpnext_str = n->wpnext_str;
	} else{
		h->zen_str[i] = n->wpnext_str;
	}
}

void wpxhash_si_free(wpxht_si h){
	if(h != NULL){
		free(h);
	}
}


void wpxhash_si_walk(wpxht_si h, wpxhash_si_walker w, void *arg){
	register unsigned int i;
	register wpxhn_si n,t;

	for(i = 0; i < h->prime; i++){
		for(n = h->zen_str[i]; n != NULL; ){
			t = n;
			n = t->wpnext_str;
			(*w)(h, t->wpkey_str, t, arg);
		}
	}
}

void wpxhash_si_walk_i(wpxht_si h, wpxhash_si_walker_i w, void *arg){
	register unsigned int i;
	register wpxhn_si n,t;

	for(i = 0; i < h->prime; i++){
		for(n = h->zen_int[i]; n != NULL; ){
			t = n;
			n = t->wpnext_int;
			(*w)(h, t->wpkey_int, t, arg);
		}
	}
}


/**************************************************************************/
/**************************************************************************/
/**************************************************************************/
/**************************************************************************/

wpxht_list wpxhash_list_new(unsigned int prime){
	register wpxht_list xnew;

	xnew = (wpxht_list) malloc(sizeof(_wpxht_list) + sizeof(wpxhn_list)*prime);
	memset(xnew, 0, sizeof(_wpxht_list) + sizeof(wpxhn_list)*prime);
	xnew->prime = prime;
	xnew->zen = (wpxhn_list*)((unsigned char *)xnew + sizeof(_wpxht_list));
	xnew->first = NULL;
	xnew->last  = NULL;
	return xnew;
}

wpxht_list wpxhash_list_new_pool(unsigned int prime, pool p){
	register wpxht_list xnew;

	xnew = (wpxht_list) pmalloco(p, sizeof(_wpxht_list) + sizeof(wpxhn_list)*prime);
	xnew->prime = prime;
	xnew->zen = (wpxhn_list*)((unsigned char *)xnew + sizeof(_wpxht_list));
	xnew->first = NULL;
	xnew->last  = NULL;
	return xnew;
}

int wpxhash_list_put(wpxht_list h, char *key, void *val){
	wpxhn_list n,lastn;
	register unsigned int i;

	i = hasher_def(key);

	((wpxhn_list)val)->wpnext = NULL;
	((wpxhn_list)val)->wpnext_list = NULL;
	((wpxhn_list)val)->wpprev_list = h->last;;
	if (h->first==NULL) h->first = val;

	if (h->zen[i] == NULL){
		h->zen[i] = (wpxhn_list)val;
		((wpxhn_list)val)->wpkey = key;
		if (h->last) h->last->wpnext_list = val;
		h->last = val;
		h->count++;
		return 1;
	}
	//XXX dalsza obsluga listy

	lastn = NULL;
	for(n = h->zen[i]; n != NULL; n = n->wpnext){
		if(strcmp(key, n->wpkey) == 0)
			break;
		lastn = n;
	}

	/* if found n is founded, lastn is last element on n */
	if (n != NULL){
		/* replace old */

		/* if not first */
		if (lastn){
			lastn->wpnext = (wpxhn_list)val;
		} else{
			h->zen[i] = (wpxhn_list)val;
		}

		/* set data */
		((wpxhn_list)val)->wpnext = n->wpnext;

		((wpxhn_list)val)->wpkey = key;
		return 2;
	}

	lastn->wpnext = (wpxhn_list)val;
	((wpxhn_list)val)->wpkey = key;
	h->count++;

	return 2;
}


void *wpxhash_list_get(wpxht_list h, char *key){
	register wpxhn_list n;
	register unsigned int i;

	i = hasher_def(key);

	for(n = h->zen[i]; n != NULL; n = n->wpnext){
		if(j_strcmp(key, n->wpkey) == 0)
			return n;
	}
	return NULL;
}

void wpxhash_list_zap(wpxht_list h, char *key){
	wpxhn_list n,lastn;
	register unsigned int i;

	i = hasher_def(key);

	lastn = NULL;
	for(n = h->zen[i]; n != NULL; n = n->wpnext){
		if(strcmp(key, n->wpkey) == 0)
			break;
		lastn = n;
	}

	if (!n) return;

	/* if not first */
	if (lastn){
		lastn->wpnext = n->wpnext;
	} else{
		h->zen[i] = n->wpnext;
	}
	h->count--;
}

void wpxhash_list_free(wpxht_list h){
	if(h != NULL){
		free(h);
	}
}

void wpxhash_list_walk(wpxht_list h, wpxhash_list_walker w, void *arg){
	register wpxhn_list n,t;

	n = h->first;
	while (n){
		t = n;
		n = t->wpnext_list;
		(*w)(h, t->wpkey, t, arg);
	}
}

HASHTABLE_I wpxhash_i_new(unsigned int prime){//{{{
	register wpxhti xnew;

	xnew = (wpxhti) malloc(sizeof(_wpxhti) + sizeof(wpxhni)*prime);
	memset(xnew, 0, sizeof(_wpxht) + sizeof(wpxhni)*prime);
	xnew->prime = prime;
	xnew->zen = (wpxhni*)((unsigned char *)xnew + sizeof(_wpxhti));
	return xnew;
}//}}}
int wpxhash_i_put(HASHTABLE_I h, int key, void *val){//{{{
	wpxhni n,lastn;
	register unsigned int i;

	i = hasher_i_def(key);

	((wpxhni)val)->wpnext = NULL;

	if (h->zen[i] == NULL){
		h->zen[i] = (wpxhni)val;
		((wpxhni)val)->wpkey_int = key;
		h->count++;
		return 1;
	}

	lastn = NULL;
	for(n = h->zen[i]; n != NULL; n = n->wpnext){
		if(key == n->wpkey_int)
			break;
		lastn = n;
	}

	/* if found n is founded, lastn is last element on n */
	if (n != NULL){
		/* replace old */

		/* if not first */
		if (lastn){
			lastn->wpnext = (wpxhni)val;
		} else{
			h->zen[i] = (wpxhni)val;
		}

		/* set data */
		((wpxhni)val)->wpnext = n->wpnext;

		((wpxhni)val)->wpkey_int = key;
		return 2;
	}

	lastn->wpnext = (wpxhni)val;
	((wpxhni)val)->wpkey_int = key;
	h->count++;

	return 1;
}//}}}
void *wpxhash_i_get(HASHTABLE_I h, int key){//{{{
	register wpxhni n;
	register unsigned int i;

	i = hasher_i_def(key);

	for(n = h->zen[i]; n != NULL; n = n->wpnext){
		if(n->wpkey_int == key)
			return n;
	}
	return NULL;
}//}}}
void wpxhash_i_zap(HASHTABLE_I h, int key){//{{{
	wpxhni n,lastn;
	register unsigned int i;

	i = hasher_i_def(key);

	lastn = NULL;
	for(n = h->zen[i]; n != NULL; n = n->wpnext){
		if(n->wpkey_int == key)
			break;
		lastn = n;
	}

	if (!n) return;

	/* if not first */
	if (lastn){
		lastn->wpnext = n->wpnext;
	} else{
		h->zen[i] = n->wpnext;
	}
	h->count--;
}//}}}
void wpxhash_i_free(HASHTABLE_I h){//{{{
	if(h != NULL){
		free(h);
	}
}//}}}

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 */
