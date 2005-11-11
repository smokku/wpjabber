
#include "jabberd.h"

#define MAX_LOG_SIZE 4096

int debug_flag = 0;

/** function returns timestamp */
char *debug_log_timestamp(char *buf)
{
	time_t t;
	int sz;

	t = time(NULL);

	if (t == (time_t) - 1)
		return "[no time available]";

	ctime_r(&t, buf);
	sz = strlen(buf);
	buf[sz - 1] = ' ';
	return buf;
}

/** debug function. debug is disabled when server is compiled with NODEBUG define */
void debug_log(const char *msgfmt, ...)
{
	va_list ap;
	char message[MAX_LOG_SIZE + 1];
	char buf[100];

	va_start(ap, msgfmt);
	vsnprintf(message, MAX_LOG_SIZE, msgfmt, ap);
#ifdef SUNOS
#define DEBUG_TID (unsigned int)pthread_self()
#else
#define DEBUG_TID getpid()
#endif

	fprintf(stdout, "[%d] %s %s\n", DEBUG_TID,
		debug_log_timestamp(buf), message);
	fflush(stdout);
}
