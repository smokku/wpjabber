/* --------------------------------------------------------------------------
 *
 *  WP
 *
 * --------------------------------------------------------------------------*/

#ifndef max
#define max(a,b)	    (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a,b)	    (((a) < (b)) ? (a) : (b))
#endif


/* sec */
#define DEF_IOCLOSE 60*3     /* 3 min - time when socket is closed */
#define DEF_PINGTIME 60*2    /* 2 min - how often ping socket */
#define DEF_IOCHECK 60	     /* 1 min - how often check pings */
#define DEF_AUTHTIME 60*2    /* 2 min to auth */
#define DEF_DATACHECK 1      /* 1 s check sockets with data */
#define DEF_RECONNECTTIME  60*5  /* 5 min to reconnect server */

#define SERVER_SOCKET_BUFFERS 262144
#define CLIENT_SOCKET_BUFFER  8192

#define CLIENT_HASH_SIZE 60000
#define DEF_MAX_CLIENTS 16000 /* max clients */
#define DEF_EPOLL_SIZE 1000	 /* initial epoll set size */
#define DEF_EPOLL_MAXEVENTS 2000 /* maximal number of epoll events returned by epoll_wait() */

#define DEF_READ_SERVER 30    /* in client loop how often read from server */

#define OVERLOAD_RET	   30	  /* queue long to server */
#define OVERLOAD_NORMAL    200	  /* queue long to server */
#define OVERLOAD_CRITICAL  2000   /* queue long to server */


#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/un.h>

#include <sys/select.h>
#include <strings.h>
#include <sys/ioctl.h>

#include <sys/timeb.h>


#include <asm/page.h>
#include <sys/mman.h>

//#include <linux/eventpoll.h>
#include <sys/poll.h>

#ifdef OWN_SYS_EPOLL
#include "epoll.h"
#else
#include <sys/epoll.h>
#endif



#include <pthread.h>
#ifdef __cplusplus
extern "C" {
#endif
#include <lib.h>
#ifdef __cplusplus
}
#endif

#define VERSION "WPJ v 1.1. Socket multiplexer by Lukas Karwacki"

#define MAX_MEM_LOG_MESSAGES 100
#define MAX_MEM_LOG_MSG_SIZE 3000
#define ut(t) ((long long)((t).tv_sec)*1000000 + (t).tv_usec)

volatile extern int wpj_shutdown;

typedef enum { p_NONE, p_NORM, p_XDB, p_LOG, p_ROUTE } ptype;

typedef unsigned long DWORD;
typedef unsigned char BOOL;

DWORD timeGetTime();
DWORD timeGetTimeSec100();
time_t timeGetTimeSec();
void Sleep(DWORD time);

#include "debug.h"

/************************** LOG **************************/
void log_notice(char *host, const char *msgfmt, ...);
void log_warn(char *host, const char *msgfmt, ...);
void log_alert(char *host, const char *msgfmt, ...);
void init_log(char * log_file);
void stop_log();
#define log_error log_alert

/************************** IO **************************/

/* struct to handle the write queue */
typedef enum { queue_XMLNODE, queue_CDATA } io_queue_type;
typedef struct io_wb_q_st
{
    pool p;
    io_queue_type type;
    xmlnode x;
    char *data;
    char *cur;
    int len;
    struct io_wb_q_st *next;
} _io_wbq,*io_wbq;

typedef struct io_sended_struct
{
  xmlnode x;
  time_t t;   /* time of add */
  struct io_sended_struct *next;
} _io_sended,*io_sended;

/* the io data type */
typedef enum { state_ACTIVE, state_CLOSE } io_state;
typedef enum { type_LISTEN, type_NORMAL, type_NUL, type_HTTP, type_SERVER } io_type;

struct io_handlers_st;
typedef struct io_st
{
  io_type type;
  volatile io_state state;
  unsigned char root;
  unsigned char is_data;	     /* 0 - no data, 1 - data */
  unsigned char ping_in_progress;    /* if pinging */

  unsigned char accepted;    /* socket is always accepted, and later it will
								be recognized is it SSL or normal connection
								or HTTP GET request */
  int write_buffer_count;    /* write buffer size */

  pool p;
  int fd;
  int pollpos;

  volatile io_wbq queue; /* write buffer queue */
  volatile io_wbq tail;  /* the last buffer queue item */

  /* sended queue, to prevent from losing packages */
  io_sended s_queue; /* sended queue */
  io_sended s_tail;  /* the last sended queue item */

  int queue_long;

  struct io_st *prev,*next;
  struct io_st *dataprev,*datanext;

  pthread_mutex_t sem;

  void *cb_arg;
  void *cb;
  struct io_handlers_st *mh;

#ifdef HAVE_SSL
  void	     *ssl;
  int	     error;
#endif

  XML_Parser parser;
  xmlnode    stacknode;

  time_t last_io;	  /* last read or ping */

  struct karma k;
  struct in_addr sin_addr;
  char *ip;
} *io, _io;

typedef ssize_t (*io_read_func)    (io m, void*		   buf,       size_t	 count);
typedef ssize_t (*io_write_func)   (io m, const void*	   buf,       size_t	 count);
typedef void	(*io_parser_func)  (io m,  const void*	    buf,       size_t	  bufsz);
typedef int	(*io_accept_func)  (io m, struct sockaddr* serv_addr, socklen_t* addrlen);
typedef int	(*io_connect_func) (io m, struct sockaddr* serv_addr, socklen_t  addrlen);

/* the IO handlers data type */
typedef struct io_handlers_st
{
    io_read_func   read;
    io_write_func  write;
    io_accept_func accept;
    io_parser_func parser;
} _io_handlers, *io_handlers;

/* standard read/write/accept/connect functions */
#define IO_READ_FUNC	read
#define IO_WRITE_FUNC	write
#define IO_ACCEPT_FUNC	accept
#define IO_CONNECT_FUNC connect

ssize_t _io_raw_read(io m, void *buf, size_t count);
ssize_t _io_raw_write(io m, void *buf, size_t count);
int _io_raw_accept(io m, struct sockaddr* serv_addr, socklen_t* addrlen);
void _io_raw_parser(io m, const void *buf, size_t bufsz);
int _io_raw_connect(io m, struct sockaddr* serv_addr, socklen_t  addrlen);
#define IO_RAW_READ    (io_read_func)&_io_raw_read
#define IO_RAW_WRITE   (io_write_func)&_io_raw_write
#define IO_RAW_ACCEPT  (io_accept_func)&_io_raw_accept
#define IO_RAW_CONNECT (io_connect_func)&_io_raw_connect
#define IO_RAW_PARSER  (io_parser_func)&_io_raw_parser

void _io_xml_parser(io m, const void *buf, size_t bufsz);
#define IO_XML_PARSER  (io_parser_func)&_io_xml_parser

/* callback state flags */
#define IO_NEW	     0
#define IO_BUFFER    1
#define IO_XML_ROOT  2
#define IO_XML_NODE  3
#define IO_CLOSED    4
#define IO_ERROR     5

#ifdef HAVE_SSL
/* SSL functions */
int io_ssl_init(xmlnode x);
void io_ssl_stop();
void	_io_ssl_cleanup (void *arg);
ssize_t _io_ssl_read	(io m, void *buf, size_t count);
ssize_t _io_ssl_write	(io m, const void*	buf,	   size_t     count);
int	_io_ssl_accept	(io m, struct sockaddr* serv_addr, socklen_t* addrlen);
#define IO_SSL_READ    _io_ssl_read
#define IO_SSL_WRITE   _io_ssl_write
#define IO_SSL_ACCEPT  _io_ssl_accept
#endif

/* standard i/o callback function definition */
typedef void (*io_std_cb)(io m, int state, void *arg);
typedef void (*io_xml_cb)(io m, int state, void *arg, xmlnode x);
typedef void (*io_raw_cb)(io m, int state, void *arg, char *buffer,int bufsz);
#ifdef HAVE_SSL
typedef void (*io_ssl_cb)(io m, int state, void *arg, char *buffer,int bufsz);
#endif

/* initializes the IO subsystem */
void io_init(void);

/* stops the IO system */
void io_stop(void);

/* create a new io object from a file descriptor */
io io_new(int fd, void *cb, void *cb_arg);

/* request the io socket be closed */
void io_close(io m);

/* writes an xmlnode to the socket */
void io_write(io m,xmlnode x, char *buffer, int len);

/* sets the karma values for a socket */
void io_karma(io m, int val, int max, int inc, int dec, int penalty, int restore);
void io_karma2(io m, struct karma *k);


/* pops the next xmlnode from the queue, or NULL if no more nodes */
void io_cleanup(io m);

/* connects to an ip */
void io_connect(char *host, int port, void *cb, void *cb_arg, int timeout);

/* starts listening on a port/ip, returns NULL if failed to listen */
io io_listen(int port, char *listen_host, void *cb, void *arg);

/* some nice api utilities */
#define io_pool(m) (m->p)
#define io_ip(m) (m->ip)

/* ********************** SERVER & CLIENT ****************************/

typedef enum { conn_DIE, conn_CLOSED, conn_OPEN, conn_AUTHD,conn_CLOSING } conn_state;
typedef enum { state_CLOSING, state_UNKNOWN, state_AUTHD, state_REFRESHING_SESSION } client_state;

void server_process_xml(io m, int state, void* arg, xmlnode x);
void client_process_xml(io m, int state, void* arg, xmlnode x);

typedef struct cqueue_struct
{
    xmlnode x;
    struct cqueue_struct *next;
    void* arg;
} *cqueue, _cqueue;

struct client_struct
{
    int loc;
    io m;
    client_state state;
    jid host;
    jid orghost;
    struct {
      char* username;
      char* resource;
      xmlnode presence;
    } real_user;
    char *id;
    char *res;
    char *sid;
    char *auth_id;
    cqueue q_start,q_end;
    time_t time; /* time of connection later  time of auth */
    struct client_struct *next;
};


typedef struct client_struct *client_c , _client_c;

struct server_struct
{
  char *secret;
  char *mhost;
  int mport;
  char *mip;
  conn_state state;
  char servertime[50];
  cqueue q_auth_start,q_auth_end;
  volatile io m;
};

typedef struct server_struct *server_info, _server_info;

/* ************************* CONFIG ************************* */
struct wp_connection_struct
{
  char *secret;
  char *mhost;
  int mport;
  char *mip;
  struct wp_connection_struct *next;
};

typedef struct wp_connection_struct *wp_connection, _wp_connection;

struct config_struct {
  char *hostname;
  int lport;
  char *lip;
  char * pidfile;
  char * logfile;
  char * statfile;
  char * httpforward;
  int conn_number;	 /* number of server to connect to */
  //  char * reg_host;	     /* server for new users */
  int max_clients;
  int auto_refresh_sessions; /* flag session recovery after server restart */
  int no_message_storage;   /* flag no message recovery */
  int ping_time;
  int iocheck;
  int ioclose;
  int datacheck;
  int auth_time;
  int reconnect_time;
  DWORD karma_time;
  volatile server_info si;
  int epoll_size;	/* initial epoll set size */
  int epoll_maxevents;	/* maximal number of epoll events returned by epoll_wait() */
#ifdef HAVE_SSL
  int ssl;
#endif
  wp_connection connection;
};

typedef struct config_struct *config, _config;

/* main structure wpj */
typedef struct wpj_struct
{
  pool p;
  config cfg;
  _client_c clients[DEF_MAX_CLIENTS];
  client_c first_free_client;
  client_c last_free_client;
  server_info si;
  int clients_count;
#ifdef HAVE_SSL
  int clients_ssl;
#endif
  int overload;
} _wpjs, *wpjs;


typedef struct io_main_st
{
    io master__list;	   /* a list of all the socks */
    io data__list;	   /* a list of all the socks */
    int list_count;

    volatile io listenio;
    volatile io serverio;

    DWORD refresh_start;
    int refresh_flag;	/* 1 - wait for server to connect */

    pthread_t t;	/* thread number */
    pthread_t wt;	/* server write thread */
    volatile int shutdown;  /* flag to shutdown */
    DWORD karma_time;	/* karma update time */
    int epollfd;	/* sys_epoll */
    struct epoll_event * evp; /* epoll events array */
} _ios,*ios;


/* RATE */
void RateInit(int conn,int time,int count);
void RateStop();
int RateCheckAdd(struct in_addr sin_addr);
int RateRemove(struct in_addr sin_addr); /* remove rate */






