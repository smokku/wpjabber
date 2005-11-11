
#define THREAD_INC(X);	X++;
#define THREAD_DEC(X);	X--;

#define SEM_VAR pthread_mutex_t
#define COND_VAR pthread_cond_t
#define THREAD_VAR pthread_t
#define SEM_INIT(x) pthread_mutex_init(&x,NULL)
#define SEM_LOCK(x) pthread_mutex_lock(&x)
#define SEM_TRY_LOCK(x) pthread_mutex_trylock(&x)
#define SEM_UNLOCK(x) pthread_mutex_unlock(&x)
#define SEM_DESTROY(x) pthread_mutex_destroy(&x)
#define COND_INIT(x) pthread_cond_init(&x,NULL)
#define COND_SIGNAL(x) pthread_cond_signal(&x)
#define COND_BROADCAST(x) pthread_cond_broadcast(&x)
#define COND_WAIT(x,s) pthread_cond_wait(&x,&s)
#define	THREAD_CREATE(t,f,a) pthread_create(&t, NULL, f, a)
#define THREAD_JOIN(t,r) pthread_join(t,&r)
#define THREAD_CREATE_DETACHED(t,f,a){ \
pthread_attr_t attr;\
pthread_attr_init(&attr);\
pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);\
pthread_create(&t, &attr, f, a);\
pthread_attr_destroy(&attr); }
