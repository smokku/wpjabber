
#ifndef WP_QUEUE_H
#define	WP_QUEUE_H


/*
 * Circular queue declarations.
 */
#define	WPCIRCLEQ_HEAD(type)						\
struct {								\
	struct type *cqe;		/* start element */		\
}

#define	WPCIRCLEQ_ELEMENT(type)						\
struct {								\
	struct type *cqe_next;		/* next element */		\
	struct type *cqe_prev;		/* previous element */		\
}

/*
 * Circular queue functions.
 */
#define	WPCIRCLEQ_EMPTY(head)	((head).cqe == NULL)

#define	WPCIRCLEQ_FIRST(head)	((head).cqe)

#define	WPCIRCLEQ_LAST(head,field)	((head).cqe->field.cqe_prev)

#define	WPCIRCLEQ_NEXT(elm,field)	((elm)->field.cqe_next)

#define	WPCIRCLEQ_PREV(elm,field)	((elm)->field.cqe_prev)

#define	WPCIRCLEQ_INIT(head)						\
	WPCIRCLEQ_FIRST((head)) = NULL;

#define	WPCIRCLEQ_REMOVE(head, elm, field){				\
	if (WPCIRCLEQ_FIRST((head)) == (elm)){				\
	   if (WPCIRCLEQ_NEXT((elm), field) == elm)			\
	       WPCIRCLEQ_FIRST((head)) = NULL;				\
	   else								\
	       WPCIRCLEQ_FIRST((head)) = WPCIRCLEQ_NEXT((elm), field);}	\
	if (!WPCIRCLEQ_EMPTY(head)){				       \
		WPCIRCLEQ_PREV(WPCIRCLEQ_NEXT((elm), field), field) =	\
		    WPCIRCLEQ_PREV((elm), field);			\
		WPCIRCLEQ_NEXT(WPCIRCLEQ_PREV((elm), field), field) =	\
		    WPCIRCLEQ_NEXT((elm), field);			\
	}								\
}

#define	WPCIRCLEQ_INSERT_HEAD(head, elm, field){			\
	if (WPCIRCLEQ_EMPTY(head)){				       \
	  WPCIRCLEQ_NEXT((elm), field) = elm;				\
	  WPCIRCLEQ_PREV((elm), field) = elm;				\
	} else{							       \
	  WPCIRCLEQ_PREV((elm), field) = WPCIRCLEQ_LAST((head),field);	\
	  WPCIRCLEQ_NEXT(WPCIRCLEQ_LAST((head),field),field) = elm;		\
	  WPCIRCLEQ_NEXT((elm), field) = WPCIRCLEQ_FIRST(head);		\
	  WPCIRCLEQ_PREV(WPCIRCLEQ_FIRST((head)),field) = elm;		\
	}								\
	WPCIRCLEQ_FIRST(head) = elm;					\
}


#define	WPCIRCLEQ_FOREACH(var, head, field, counter)			\
	for ((counter)=0,(var) = WPCIRCLEQ_FIRST((head));		\
	    ( (((counter)==0) && ((var))) ||   \
	     ( ((counter)>0) && ((var) != WPCIRCLEQ_FIRST((head))) ) );     \
	    (var) = WPCIRCLEQ_NEXT((var), field),(counter)++)

/*
#undef CIRCLEQ_FOREACH_REVERSE
#define	CIRCLEQ_FOREACH_REVERSE(var, head, field)			\
	for ((var) = CIRCLEQ_LAST((head));				\
	    (var) != (void *)(head) || ((var) = NULL);			\
	    (var) = CIRCLEQ_PREV((var), field))

*/

#endif
