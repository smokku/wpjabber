
typedef void (*fast_mtq_cb) (void *arg, void *value);

typedef void *(*fast_mtq_start_cb) (void *arg);

typedef void *(*fast_mtq_stop_cb) (void *arg);

#define FAST_MTQ_ELEMENT struct fast_mtq_elem_struct *mtq_next; \
  char mtq_data[0];

typedef struct fast_mtq_elem_struct {
	FAST_MTQ_ELEMENT;
} *fast_mtq_elem, _fast_mtq_elem;

typedef struct fast_mtq_struct {
	/* callback function */
	fast_mtq_cb cb;
	/* callback function */
	fast_mtq_start_cb start_cb;
	/* callback function */
	fast_mtq_stop_cb stop_cb;
	/* list */
	fast_mtq_elem first, last;
	/* argument */
	void *arg;
	/* number of threads */
	int threads;
	/* shutdwon flag */
	volatile int shutdown;
	/* number of element in queue */
	volatile int queue;
	/* sem */
	SEM_VAR sem;
	/* cond */
	COND_VAR cond;
	/* number of element in queue */
	volatile int queue2;
	/* threads */
	THREAD_VAR threads_table[0];
} *fast_mtq, _fast_mtq;


inline static int fast_mtq_queue_len(fast_mtq f_mtq)
{
	return f_mtq->queue;
}

void fast_mtq_send(fast_mtq f_mtq, void *arg);
void fast_mtq_stop(fast_mtq f_mtq);
void fast_mtq_stop_fast(fast_mtq f_mtq);

fast_mtq fast_mtq_init(int threads, fast_mtq_cb cb, void *arg);

fast_mtq fast_mtq_start_init(int threads, fast_mtq_cb cb, void *arg,
			     fast_mtq_start_cb start_cb);
fast_mtq fast_mtq_start_stop_init(int threads, fast_mtq_cb cb, void *arg,
				  fast_mtq_start_cb start_cb,
				  fast_mtq_stop_cb stop_cb);

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 */
