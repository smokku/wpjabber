
#ifdef __cplusplus
extern "C" {
#endif


/* --------------------------------------------------------- */
/*							     */
/* WP Hashtable						     */
/*							     */
/* --------------------------------------------------------- */

#define HASHTABLE wpxht
#define HASHTABLE_I wpxhti

#define HASHTABLE_SI wpxht_si

#define HASHTABLE_LIST wpxht_list

#define WPHASH_BUCKET struct wpxhn_struct *wpnext; \
	char *wpkey

#define WPHASH_I_BUCKET struct wpxhni_struct *wpnext; \
	int wpkey_int

#define WPHASH_SI_BUCKET struct wpxhn_si_struct *wpnext_str; \
	struct wpxhn_si_struct *wpnext_int; \
	unsigned int wpkey_int; \
	char *wpkey_str

#define WPHASH_LIST_BUCKET struct wpxhn_list_struct *wpnext; \
	struct wpxhn_list_struct *wpnext_list; \
	struct wpxhn_list_struct *wpprev_list; \
	char *wpkey

typedef struct wpxhn_struct
{
	WPHASH_BUCKET;
} *wpxhn, _wpxhn;

typedef struct wpxhni_struct
{
	WPHASH_I_BUCKET;
} *wpxhni, _wpxhni;

typedef struct wpxhn_si_struct
{
	WPHASH_SI_BUCKET;
} *wpxhn_si, _wpxhn_si;

typedef struct wpxhn_list_struct
{
	WPHASH_LIST_BUCKET;
} *wpxhn_list, _wpxhn_list;

typedef struct wpxhn_char_struct
{
  WPHASH_BUCKET;
  char *value;
} *wpxhn_char, _wpxhn_char;

typedef struct wpxht_struct
{
  wpxhn *zen;
  unsigned int count;
  unsigned int prime;
} *wpxht, _wpxht;

typedef struct wpxht_i_struct
{
  wpxhni *zen;
  unsigned int count;
  unsigned int prime;
} *wpxhti, _wpxhti;

typedef struct wpxht_si_struct
{
  wpxhn_si *zen_str;
  wpxhn_si *zen_int;
  unsigned int count;
  unsigned int prime;
} *wpxht_si, _wpxht_si;

typedef struct wpxht_list_struct
{
  wpxhn_list *zen;
  wpxhn_list first,last;
  unsigned int prime;
  unsigned int count;
} *wpxht_list, _wpxht_list;

typedef int (*wpghash_walker)(void *user_data, void *key, void *data);
typedef void (*wpxhash_walker)(wpxht h, char *key, void *val, void *arg);

typedef void (*wpxhash_list_walker)(wpxht_list h, char *key, void *val, void *arg);

typedef void (*wpxhash_si_walker)(wpxht_si h, char *key, void *val, void *arg);
typedef void (*wpxhash_si_walker_i)(wpxht_si h, unsigned int key, void *val, void *arg);

wpxht wpxhash_new(unsigned int prime);
/** alocate memory from pool */
wpxht wpxhash_new_pool(unsigned int prime, pool p);
int wpxhash_put(wpxht h, char *key, void *val);
void *wpxhash_get(wpxht h, char *key);
char *wpxhash_get_value(wpxht h, char *key);
void wpxhash_zap(wpxht h, char *key);
void wpxhash_free(wpxht h);

void wpxhash_walk(wpxht h, wpxhash_walker w, void *arg);
void wpghash_walk(wpxht h, wpghash_walker w, void *arg);

/* hash with list */
wpxht_list wpxhash_list_new(unsigned int prime);
wpxht_list wpxhash_list_new_pool(unsigned int prime, pool p);
int wpxhash_list_put(wpxht_list h, char *key, void *val);
void *wpxhash_list_get(wpxht_list h, char *key);
void wpxhash_list_zap(wpxht_list h, char *key);
void wpxhash_list_free(wpxht_list h);
void wpxhash_list_walk(wpxht_list h, wpxhash_list_walker w, void *arg);

/* Str int */
wpxht_si wpxhash_si_new(unsigned int prime);
wpxht_si wpxhash_si_new_pool(unsigned int prime, pool p);
int wpxhash_si_put(wpxht_si h, char *str_key, unsigned int int_key, void *val);
void *wpxhash_si_get(wpxht_si h, char *str_key);
void *wpxhash_si_get_i(wpxht_si h, unsigned int int_key);
void wpxhash_si_zap(wpxht_si h, char *str_key);
void wpxhash_si_zap_i(wpxht_si h, unsigned int int_key);
void wpxhash_si_free(wpxht_si h);

void wpxhash_si_walk(wpxht_si h, wpxhash_si_walker w, void *arg);
void wpxhash_si_walk_i(wpxht_si h, wpxhash_si_walker_i w, void *arg);

/*int*/
HASHTABLE_I wpxhash_i_new(unsigned int prime);
int wpxhash_i_put(HASHTABLE_I h, int key, void *val);
void *wpxhash_i_get(HASHTABLE_I h, int key);
void wpxhash_i_zap(HASHTABLE_I h, int key);
void wpxhash_i_free(HASHTABLE_I h);

#ifdef __cplusplus
}
#endif

