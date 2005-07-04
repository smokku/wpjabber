
#include "jabberd.h"

void *fast_mtq_thread(void *arg)
{
	fast_mtq f_mtq = (fast_mtq) arg;
	register fast_mtq_elem e;
	void *cb_arg;

	cb_arg = f_mtq->arg;

	if (f_mtq->start_cb) {
		cb_arg = (f_mtq->start_cb) (cb_arg);
	}

	log_debug("fast mtq thread start");

	while (1) {

		/* check if something to resolv */
		SEM_LOCK(f_mtq->sem);

		e = NULL;

		if (f_mtq->shutdown != 2) {
			if ((!(f_mtq->first)) && (!(f_mtq->shutdown))) {
				COND_WAIT(f_mtq->cond, f_mtq->sem);
			}

			/* get element */
			e = f_mtq->first;
			if (e) {
				f_mtq->first = e->mtq_next;
				f_mtq->queue--;
				f_mtq->queue2--;
			}
		}

		SEM_UNLOCK(f_mtq->sem);

		if (e) {
			(f_mtq->cb) (cb_arg, (void *) e);
		} else {
			if (f_mtq->shutdown)
				break;
		}
	}

	log_alert("mtq", "fast mtq thread stop %d [%d][%d]",
		  f_mtq->shutdown, f_mtq->queue, f_mtq->queue2);

	if (f_mtq->stop_cb) {
		(f_mtq->stop_cb) (cb_arg);
	}

	return NULL;
}

void fast_mtq_send(fast_mtq f_mtq, void *arg)
{
	fast_mtq_elem e = (fast_mtq_elem) arg;

	if (f_mtq->shutdown)
		return;

	e->mtq_next = NULL;

	SEM_LOCK(f_mtq->sem);

	if (f_mtq->first) {
		f_mtq->last->mtq_next = e;
	} else {
		f_mtq->first = e;
	}
	f_mtq->last = e;

	f_mtq->queue++;
	f_mtq->queue2++;

	COND_SIGNAL(f_mtq->cond);

	SEM_UNLOCK(f_mtq->sem);
}

fast_mtq fast_mtq_init(int threads, fast_mtq_cb cb, void *arg)
{
	fast_mtq f_mtq;
	int i;

	log_debug("Fast mtq init, threads:%d", threads);

	f_mtq =
	    (fast_mtq) malloc(sizeof(_fast_mtq) +
			      (threads * sizeof(THREAD_VAR)));
	memset(f_mtq, 0, sizeof(_fast_mtq));
	f_mtq->threads = threads;
	f_mtq->cb = cb;
	f_mtq->arg = arg;
	f_mtq->shutdown = 0;
	f_mtq->first = NULL;
	f_mtq->last = NULL;
	f_mtq->queue = 0;

	SEM_INIT(f_mtq->sem);
	COND_INIT(f_mtq->cond);

	for (i = 0; i < f_mtq->threads; i++) {
		THREAD_CREATE(f_mtq->threads_table[i], fast_mtq_thread,
			      f_mtq);
	}

	return f_mtq;
}

fast_mtq fast_mtq_start_init(int threads, fast_mtq_cb cb, void *arg,
			     fast_mtq_start_cb start_cb)
{
	fast_mtq f_mtq;
	int i;

	log_debug("Fast mtq init, threads:%d", threads);

	f_mtq =
	    (fast_mtq) malloc(sizeof(_fast_mtq) +
			      (threads * sizeof(THREAD_VAR)));
	memset(f_mtq, 0, sizeof(_fast_mtq));
	f_mtq->threads = threads;
	f_mtq->cb = cb;
	f_mtq->start_cb = start_cb;
	f_mtq->arg = arg;
	f_mtq->shutdown = 0;
	f_mtq->first = NULL;
	f_mtq->last = NULL;
	f_mtq->queue = 0;

	SEM_INIT(f_mtq->sem);
	COND_INIT(f_mtq->cond);

	for (i = 0; i < f_mtq->threads; i++) {
		THREAD_CREATE(f_mtq->threads_table[i], fast_mtq_thread,
			      f_mtq);
	}

	return f_mtq;
}

fast_mtq fast_mtq_start_stop_init(int threads, fast_mtq_cb cb, void *arg,
				  fast_mtq_start_cb start_cb,
				  fast_mtq_stop_cb stop_cb)
{
	fast_mtq f_mtq;
	int i;

	log_debug("Fast mtq init, threads:%d", threads);

	f_mtq =
	    (fast_mtq) malloc(sizeof(_fast_mtq) +
			      (threads * sizeof(THREAD_VAR)));
	memset(f_mtq, 0, sizeof(_fast_mtq));
	f_mtq->threads = threads;
	f_mtq->cb = cb;
	f_mtq->start_cb = start_cb;
	f_mtq->stop_cb = stop_cb;
	f_mtq->arg = arg;
	f_mtq->shutdown = 0;
	f_mtq->first = NULL;
	f_mtq->last = NULL;
	f_mtq->queue = 0;

	SEM_INIT(f_mtq->sem);
	COND_INIT(f_mtq->cond);

	for (i = 0; i < f_mtq->threads; i++) {
		THREAD_CREATE(f_mtq->threads_table[i], fast_mtq_thread,
			      f_mtq);
	}

	return f_mtq;
}

static inline void _fast_mtq_stop(fast_mtq f_mtq, int type)
{
	int i;
	void *ret;

	log_debug("Fast mtq. Stop all threads %d", type);

	f_mtq->shutdown = type;

	COND_BROADCAST(f_mtq->cond);

	for (i = 0; i < f_mtq->threads; i++) {
		THREAD_JOIN(f_mtq->threads_table[i], ret);
	}
}

void fast_mtq_stop(fast_mtq f_mtq)
{
	_fast_mtq_stop(f_mtq, 1);
}

void fast_mtq_stop_fast(fast_mtq f_mtq)
{
	_fast_mtq_stop(f_mtq, 2);
}

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 */
