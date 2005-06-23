/* --------------------------------------------------------------------------
 *
 *  WP
 *
 * --------------------------------------------------------------------------*/

#include "wpj.h"

typedef struct io_connect_st
{
    pool p;
    char *ip;
    int port;
    void *cb;
    void *cb_arg;
    pthread_t t;
    int timeout;
    int connected;
} _connect_data,  *connect_data;

/* global object */
ios io__data = NULL;
extern wpjs wpj;

#define KARMA_HEARTBEAT_LK 2  /* 2 sec */
int KARMA_DEF_INIT    = KARMA_INIT;
int KARMA_DEF_MAX     = KARMA_MAX;
int KARMA_DEF_INC     = KARMA_INC;
int KARMA_DEF_DEC     = KARMA_DEC;
int KARMA_DEF_PENALTY = KARMA_PENALTY;
int KARMA_DEF_RESTORE = KARMA_RESTORE;

/* RATE HASHTBALE */
int DEF_RATE_TIME  = 1; /* 2 connection on 1 second */
int DEF_RATE_CONN  = 2;
int DEF_CONN  = 0; /* no limit */


void karma_decrement_lk(struct karma *k){
    /* lower our karma */
    k->val -= k->dec;

    /* if below zero, set to penalty */
    if( k->val <= 0 )
	k->val = k->penalty;

	log_debug("decrement to %d",k->val);
}

/*
 * returns 1 - k.val > 0;
 */
int karma_increment_lk(struct karma *k, DWORD time){
  int punishment_over = 0;
  DWORD dt;

  /* only increment every KARMA_HEARTBEAT seconds */
  if( ( k->last_update + KARMA_HEARTBEAT_LK > time ) && k->last_update != 0)
    goto CHECK;

  /* do not raise karma if overload */
  if (wpj->overload){
    k->last_update = time;
    goto CHECK;
  }

  dt = time - k->last_update; /* might be HEARBEAT, HEARTBEAT+1 .... */
  dt = dt / KARMA_HEARTBEAT_LK;

  /* if incrementing will raise over 0 */
  if( ( k->val < 0 ) && ( k->val + (k->inc * dt) > 0 ) )
    punishment_over = 1;

  /* increment the karma value */
  k->val += (k->inc * dt);
  if( k->val > k->max ) k->val = k->max; /* can only be so good */

  /* lower our byte count, if we have good karma */
  if( k->val > 0 ) k->bytes -= ( KARMA_READ_MAX(k->val) );
  if( k->bytes <0 ) k->bytes = 0;

  k->last_update = time;

  log_debug("karma increment to %d o %d",k->val,(k->inc * dt));

  /* our karma has *raised* to 0 */
  if( punishment_over ){
      k->val = k->restore;
	  log_debug("KARMA RESTORE");
      return 1; /* k.val > 0 */
    }

 CHECK:
  if (k->val > 0)
    return 1;
  else
    return 0;
}

int karma_check_lk(struct karma *k,long bytes_read){
    /* add up the total bytes */
    k->bytes += bytes_read;
    if( k->bytes > KARMA_READ_MAX(k->val) )
	karma_decrement_lk( k );

    /* check if it's okay */
    if( k->val <= 0 )
	return 1;

    /* everything is okay */
    return 0;
}

io client_hash[CLIENT_HASH_SIZE]; /* contents info about users fd -> m */

/* hash */
void remove_client(int fd){
  client_hash[fd]=NULL;
}

void add_client(int fd,io pos){
  if (client_hash[fd] != NULL){
    log_alert("add","error in client hash, overwrite");
  }

  client_hash[fd] = pos;
}

io find_client(int fd){
  log_debug("find client %d",fd);
  return client_hash[fd];
}

/*
 * Definitions
 */
int _io_write_dump_server(io m, int * length);

/*
 * Server writing queue thread
 */
void * write_queue_main(void *arg){
  io cur;
  int ret;
  time_t time;
  int length;

  log_debug("Write thread started");

  while (1){
	/* if we are closing down, exit the loop */
	if (io__data->shutdown == 3){
	  break;
	}

	/* sleep 1/1000  - 1000 writes per seconds */
	Sleep(1);

	/* time */
	time = timeGetTimeSec();

	/* write only to authorised servers */
	cur = io__data->serverio;
	if (cur) /* if server */
	  if (cur->state == state_ACTIVE) /* if active */
	    if (cur->queue != NULL)  /* if data */
		  {
			/* write the current buffer */
			ret = _io_write_dump_server(cur,&length);
			if (ret == -1){
			  log_alert("error","Error writing to server");
			  io_close(cur);
			}
			else{
			  if (cur->ping_in_progress){
				cur->write_buffer_count += length;
			  }
			}

			/* check overload */
		if (wpj->overload)/* 1 or 2 */
			  {
				if (cur->queue_long < OVERLOAD_RET){
				    wpj->overload = 0;
				    //	   log_alert("--","0");
				  }
				else
				  if ((wpj->overload == 2)&&(cur->queue_long < OVERLOAD_NORMAL)){
					  wpj->overload = 1;
					  //					  log_alert("--","1");
					}
			  }

		  }
  }

  return 0;
}

/*
 * epoll set ops
 */
int fdop(int fd, int op){
  struct epoll_event ev;
  ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
  ev.data.u64 = 0;
  ev.data.fd = fd;
  if ((epoll_ctl(io__data->epollfd, op, fd, &ev)) != 0){
    log_alert("epoll","fdop error");
    return -1;
  }
  return 0;
}
#define ADDFD EPOLL_CTL_ADD
#define REMFD EPOLL_CTL_DEL

/*
 * unlinks a socket from the master list
 */
void _io_unlink(io m){
    if(io__data == NULL)
	return;

    io__data->list_count--;

    if(io__data->master__list == m)
       io__data->master__list = io__data->master__list->next;

    if(m->prev != NULL)
	m->prev->next = m->next;

    if(m->next != NULL)
	m->next->prev = m->prev;
}

void _io_unlink_data(io m){
    if(io__data == NULL)
	return;

    if(io__data->data__list == m)
       io__data->data__list = io__data->data__list->datanext;

    if(m->dataprev != NULL)
	m->dataprev->datanext = m->datanext;

    if(m->datanext != NULL)
	m->datanext->dataprev = m->dataprev;
}

/*
 * links a socket to the master list
 */
void _io_link(io m){
    if(io__data == NULL)
	return;

    io__data->list_count++;

    m->next = io__data->master__list;
    m->prev = NULL;

    if(io__data->master__list != NULL)
      io__data->master__list->prev = m;

    io__data->master__list = m;
}

void _io_link_data(io m){
    if(io__data == NULL)
      return;

    m->datanext = io__data->data__list;
    m->dataprev = NULL;

    if(io__data->data__list != NULL)
      io__data->data__list->dataprev = m;

    io__data->data__list = m;
}

/*
 * Dump this socket's write queue.  tries to write
 * as much of the write queue as it can, before the
 * write call would block the server
 * returns -1 on error, 0 on success, and 1 if more data to write
 */
int _io_write_dump_client(io m){
    int len;
    io_wbq cur;

    /* try to write as much as we can */
    while(m->queue != NULL){
	cur = m->queue;

	log_debug("write_dump client writing data: %.*s", cur->len, cur->cur);

	/* write a bit from the current buffer */
		len = (*m->mh->write)(m, cur->cur, cur->len);

	/* we had an error on the write */
	if(len == 0){
		  log_alert("error","error in writing, len=0");
		  if(m->cb != NULL)
			(*(io_std_cb)m->cb)(m, IO_ERROR, m->cb_arg);

		  return -1;
	}
	if(len < 0){
	    /* if we have an error, that isn't a blocking issue */
	    if(errno != EWOULDBLOCK && errno != EINTR && errno != EAGAIN
#ifdef HAVE_SSL
			   && m->error != EAGAIN
#endif

			   ){
			  log_alert("error","error in writing, len < 0");
			  /* bounce the queue */
			  if(m->cb != NULL)
				(*(io_std_cb)m->cb)(m, IO_ERROR, m->cb_arg);

			  return -1;
	    }
	    return 1;
	}
	/* we didnt' write it all, move the current buffer up */
	else if(len < cur->len){

	    cur->cur += len;
	    cur->len -= len;
			if (m->ping_in_progress){
#ifdef HAVE_SSL
			  if (m->ssl){
				int free_size = -1;

				if (ioctl(m->fd, TIOCOUTQ, &free_size) == 0){
				  if (m->write_buffer_count <= free_size)
					m->write_buffer_count = free_size;
				}
				else{
				  log_alert("ping","error in ioctl after write");
				  return -1;
				}
			  }
			  else
#endif
				{
				  m->write_buffer_count += len;
				}
			  //			  log_alert("write","%p write buffer %d", m, m->write_buffer_count);
			}
	    return 1;
	}
	/* we wrote the entire node, kill it and move on */
	else{
	    m->queue = m->queue->next;

	    if(m->queue == NULL)
			  m->tail = NULL;

			m->queue_long--;

			/* ping */
			if (m->ping_in_progress){
#ifdef HAVE_SSL
			  if (m->ssl){
				int free_size = -1;
				if (ioctl(m->fd, TIOCOUTQ, &free_size) == 0){
				  if (m->write_buffer_count <= free_size)
					m->write_buffer_count = free_size;
				}
				else{
				  log_alert("error","error in ioctl after write");
				  return -1;
				}
			  }
			  else
#endif
				{
				  m->write_buffer_count += len;
				}

			  //log_alert("write","%p write buffer %d", m, m->write_buffer_count);
			}

			/* check cur node for xml */
			if ((wpj->cfg->no_message_storage==0)&&(cur->x)&&
				(strncmp(xmlnode_get_name(cur->x),"message",7) == 0)){
			  io_sended isq;
			  /* copy x to sended queue */
			  isq = pmalloco(cur->p,sizeof(_io_sended));
			  isq->x = cur->x;
			  isq->t = time(NULL);

			  /* put on end of queue */
			  if(m->s_tail == NULL)
				m->s_queue = isq;
			  else
				m->s_tail->next = isq;

			  m->s_tail = isq;
			}
			else{
			  pool_free(cur->p);
			}
		  }
    }
    return 0;
}

int _io_write_dump_server(io m, int * length){
    int len;
    io_wbq cur;

    length = 0;

    pthread_mutex_lock(&(m->sem));

    /* try to write as much as we can */
    while(m->queue != NULL){
	cur = m->queue;

	log_debug("write_dump serwer writing data: %.*s", cur->len, cur->cur);

	/* write a bit from the current buffer */
	len = IO_WRITE_FUNC(m->fd, cur->cur, cur->len);

	/* we had an error on the write */
	if(len == 0){
		  pthread_mutex_unlock(&(m->sem));

	    if(m->cb != NULL)
		(*(io_std_cb)m->cb)(m, IO_ERROR, m->cb_arg);

	    return -1;
	}
	if(len < 0){
	    /* if we have an error, that isn't a blocking issue */
		  if(errno != EWOULDBLOCK && errno != EINTR && errno != EAGAIN
#ifdef HAVE_SSL
			 && m->error != EAGAIN
#endif

			 ){
			pthread_mutex_unlock(&(m->sem));

			log_error("server","Write ERROR !!!");

			/* bounce the queue */
			if(m->cb != NULL)
			  (*(io_std_cb)m->cb)(m, IO_ERROR, m->cb_arg);

			return -1;
		  }
		  pthread_mutex_unlock(&(m->sem));

	    /* didn't write anything */
		  return 1;
	}
	/* we didnt' write it all, move the current buffer up */
	else if(len < cur->len){
			cur->cur += len;
	    cur->len -= len;
			pthread_mutex_unlock(&(m->sem));
			length += len;
	    return 1;
		  }
	/* we wrote the entire node, kill it and move on */
	else{
			length += len;

	    m->queue = m->queue->next;

	    if(m->queue == NULL)
			  m->tail = NULL;

	    pool_free(cur->p);
			m->queue_long--;
		  }
    }

    pthread_mutex_unlock(&(m->sem));
    return 0;
}

/*
 * internal close function
 * does a final write of the queue, bouncing and freeing all memory
 */
void _io_close(io m){
    int ret = 0;

    log_debug("_io_close");

#ifdef HAVE_SSL
	if (m->ssl)
	  wpj->clients_ssl--;
#endif

    /* ensure that the state is set to CLOSED */
    m->state = state_CLOSE;

    RateRemove(m->sin_addr);

    /* take it off the master__list */
    _io_unlink(m);
    if (m->is_data) _io_unlink_data(m);

    /* try to write what's in the queue */
    if(m->queue != NULL){
	log_debug("dumping data in _io_close");
	ret = _io_write_dump_client(m);
    }

	//    if(ret == 1) /* still more data, bounce it all */
	//	  if(m->cb != NULL)
	//	      (*(io_std_cb)m->cb)(m, IO_ERROR, m->cb_arg);

    /* notify of the close */
    if(m->cb != NULL)
	(*(io_std_cb)m->cb)(m, IO_CLOSED, m->cb_arg);

    /* cleanup the write queue */
    io_cleanup(m);

    /* close the socket, and free all memory */
    close(m->fd);

    /* free */
    pool_free(m->p);
}


void _io_close_server(io m){
    int ret = 0;
    int length;

    /* ensure that the state is set to CLOSED */
    m->state = state_CLOSE;

    /* try to write what's in the queue */
    if(m->queue != NULL){
	ret = _io_write_dump_server(m,&length);
    }

    if(ret == 1) /* still more data, bounce it all */
	if(m->cb != NULL)
	    (*(io_std_cb)m->cb)(m, IO_ERROR, m->cb_arg);

    /* notify of the close */
    if(m->cb != NULL)
	(*(io_std_cb)m->cb)(m, IO_CLOSED, m->cb_arg);

    /* close the socket, and free all memory */
    io__data->serverio = NULL;

    close(m->fd);

    /* cleanup the write queue */
    io_cleanup(m);

    pthread_mutex_destroy(&(m->sem));

    pool_free(m->p);

    log_alert("free","freed serwer IO socket");
}

void close_client(io m){
  fdop(m->fd, REMFD);
  remove_client(m->fd);
  _io_close(m);
}


void _io_close_listen(io m){
    if (m != io__data->listenio) return;

    /* remove from epoll set */
    fdop(m->fd, REMFD);

    /* close the socket, and free all memory */
    close(m->fd);

    pool_free(m->p);

    io__data->listenio = NULL;

    log_debug("freed listen IO socket");
}


/*
 * accept an incoming connection from a listen sock
 */
io _io_accept(io m){
    struct sockaddr_in serv_addr;
    size_t addrlen = sizeof(serv_addr);
    int fd;
    io new;
    int s = 4;
    int socket_size = CLIENT_SOCKET_BUFFER;

    log_debug("_io_accept calling accept on fd #%d", m->fd);

	/* if stopping */
	if (wpj_shutdown)
	  return ((void *)-1);

	/* pull a socket off the accept queue */
	fd = accept(m->fd,(struct sockaddr*)&serv_addr, (socklen_t*)&addrlen);
    if(fd <= 0){
	    return ((void *)-1);
    }

    if( wpj->clients_count >= wpj->cfg->max_clients ){
	log_warn("accept", "Max clients reached");
	close(fd);
	return ((void *)-1);
    }

    /* make sure that we aren't rate limiting this IP */
    if (RateCheckAdd(serv_addr.sin_addr)){
	log_warn("io_select", "%s is being connection rate limited", inet_ntoa(serv_addr.sin_addr));
	close(fd);
	return NULL;
    }


    log_debug("new socket accepted (fd: %d, ip: %s, port: %d)", fd, inet_ntoa(serv_addr.sin_addr), ntohs(serv_addr.sin_port));

    /* create a new sock object for this connection
	   and make it non blocking */
    new		    = io_new(fd, m->cb, m->cb_arg);

	if ((!wpj->cfg->httpforward)
#ifdef HAVE_SSL
		&& (!wpj->cfg->ssl)
#endif
		){
	  new->accepted = 1;
	}

    s = sizeof(int);
    setsockopt(new->fd,SOL_SOCKET,SO_SNDBUF,(char *)&socket_size,s);
    setsockopt(new->fd,SOL_SOCKET,SO_RCVBUF,(char *)&socket_size,s);

    socket_size = 1;
    setsockopt(new->fd,SOL_SOCKET,SO_KEEPALIVE,(char *)&socket_size,s);

	/* copy handlers */
    new->mh = pmalloco(new->p, sizeof(_io_handlers));
    new->mh->read   = m->mh->read;
    new->mh->write  = m->mh->write;
    new->mh->parser = m->mh->parser;
    new->mh->accept = m->mh->accept;

    new->ip  = pstrdup(new->p, inet_ntoa(serv_addr.sin_addr));
    new->sin_addr = serv_addr.sin_addr;

    io_karma2(new, &m->k);

    if(m->cb != NULL)
	  (*(io_std_cb)new->cb)(new, IO_NEW, new->cb_arg);

    return new;
}

void * _io_connect(void *arg){
    connect_data cd = (connect_data)arg;
    struct sockaddr_in sa;
    struct in_addr     *saddr;
    int		       flag = 1,
		       flags;
    int		       n;
    io		       new;
    pool	       p;
    struct pollfd pfdset[1];
    int		       ret = 0;
    int		       error,s=4;
    int		       socket_size = SERVER_SOCKET_BUFFERS;


    /* wait 2 seconds */
    if (cd->timeout > 0)
      sleep(cd->timeout);

    if ((!io__data) || (io__data->shutdown > 0)){
      pool_free(cd->p);
      return (void*)1;
    }

    bzero((void*)&sa, sizeof(struct sockaddr_in));

    p		= cd->p;
    new		= pmalloco(p, sizeof(_io));
    new->p	= p;
    new->type	= type_SERVER;
    new->state	= state_ACTIVE;
    new->ip	= pstrdup(p,cd->ip);
    new->cb	= (void*)cd->cb;
    new->cb_arg = cd->cb_arg;
    new->pollpos= 1;

    new->fd = socket(AF_INET, SOCK_STREAM,0);

    log_debug("Connecting server on socket %d",new->fd);

    if(new->fd < 0 || setsockopt(new->fd, SOL_SOCKET, SO_REUSEADDR, (char*)&flag, sizeof(flag)) < 0){
	if(cd->cb != NULL)
	    (*(io_std_cb)cd->cb)(new, IO_CLOSED, cd->cb_arg);
	cd->connected = -1;

	if(new->fd > 0)
	    close(new->fd);
	pool_free(p);
	return (void*)1;
    }

    flags =  fcntl(new->fd, F_GETFL, 0);
    flags |= O_NONBLOCK;
    fcntl(new->fd, F_SETFL, flags);

    saddr = make_addr(cd->ip);
    if(saddr == NULL){
	if(cd->cb != NULL)
	    (*(io_std_cb)cd->cb)(new, IO_CLOSED, cd->cb_arg);
	cd->connected = -1;

	if(new->fd > 0)
	    close(new->fd);
	pool_free(p);
	return (void*)1;
    }

    sa.sin_family = AF_INET;
    sa.sin_port = htons(cd->port);
    sa.sin_addr.s_addr = saddr->s_addr;

    if(( n = IO_CONNECT_FUNC(new->fd, (struct sockaddr*)&sa, sizeof sa)) < 0){
      if (errno != EINPROGRESS){
	if(cd->cb != NULL)
	  (*(io_std_cb)cd->cb)(new, IO_CLOSED, cd->cb_arg);
	cd->connected = -1;

	if(new->fd > 0)
	  close(new->fd);
	pool_free(p);
	return (void*)1;
      }
    }

    log_debug("Done connect");

    if (n != 0){
      pfdset[0].fd = new->fd;
      pfdset[0].events = ( POLLIN | POLLOUT | POLLHUP | POLLERR );
      pfdset[0].revents = 0;


      if ((n = poll((struct pollfd *)pfdset, 1, 10000)) == 0){
	ret = -1;
      }
      else{
	if ((pfdset[0].revents & POLLIN) || (pfdset[0].revents & POLLOUT)){
	  s=sizeof(error);
	  if (getsockopt( new->fd, SOL_SOCKET, SO_ERROR, &error, &s) < 0)
	    ret = -1;
	}
	else{
	  ret = -1;
	}
      }

      if (ret != 0 || error){
	if(cd->cb != NULL)
	  (*(io_std_cb)cd->cb)(new, IO_CLOSED, cd->cb_arg);
	cd->connected = -1;

	if(new->fd > 0)
	  close(new->fd);
	pool_free(p);
	return (void*)1;
      }
    }

    /* no decrement for servers */
    io_karma(new, KARMA_DEF_INIT, KARMA_DEF_MAX, KARMA_DEF_INC, 0, KARMA_DEF_PENALTY, KARMA_DEF_RESTORE);

    pthread_mutex_init(&(new->sem),NULL);

    cd->connected = 1;

    /* set read and write bufor to 32768 */
    s = sizeof(int);
    setsockopt(new->fd,SOL_SOCKET,SO_SNDBUF,(char *)&socket_size,s);
    setsockopt(new->fd,SOL_SOCKET,SO_RCVBUF,(char *)&socket_size,s);

    socket_size = 1;
    setsockopt(new->fd,SOL_SOCKET,SO_KEEPALIVE,(char *)&socket_size,s);

    if(new->cb != NULL)
      (*(io_std_cb)new->cb)(new, IO_NEW, new->cb_arg);

    return 0;
}

/*
 * server read
 */
int handle_server_read(){
  int len;
  char buf[SERVER_SOCKET_BUFFERS+2];

  if (!io__data->serverio) return -1;
  if (io__data->serverio->state != state_ACTIVE) return -1;

  len = IO_READ_FUNC(io__data->serverio->fd, buf, SERVER_SOCKET_BUFFERS);

  /* if we had a bad read */
  if(len == 0){
      log_alert("error","server error reading");
      io_close(io__data->serverio);
      return -1;
  }
  else if(len < 0){
	if(errno != EWOULDBLOCK && errno != EINTR && errno != EAGAIN){
	  /* kill this socket and move on */
	  log_alert("error","serwer error reading.");
	  io_close(io__data->serverio);
	  return -1;  /* loop on the same socket to kill it for real */
	}
  }
  else{
	if (len == SERVER_SOCKET_BUFFERS)
	  io__data->serverio->is_data = 1; /* mark data in server socket */
	else
	  io__data->serverio->is_data = 0;


	buf[len] = '\0';
	log_debug("IO read from serwer socket %d: %s", io__data->serverio->fd, buf);

	/* parse data */
	_io_xml_parser(io__data->serverio, buf, len);
  }
  return 0;
}

int handle_client_read(io cur){
  int len, maxlen;
  char buf[CLIENT_SOCKET_BUFFER+1];

  if (!cur->accepted){
    len = recv(cur->fd, buf, 40, MSG_PEEK);

    if (len<8) return 0;

    buf[len] = 0;

    /* if HTTP */
    if (wpj->cfg->httpforward){
      if (strstr(buf,"GET") != NULL){
	snprintf(buf,8000,"HTTP/1.0 301 Found\r\n"
		 "Server: " VERSION "\r\n"
		 "Location: %s\r\n"
		 "Pragma: no-cache\r\n"
		 "Content-Type: text/html\r\n"
		 "Content-Length: 0\r\n"
		 "Expires: Thu, 01 Jan 1970 01:00:00 GMT\r\n"
		 "Connection: close\r\n\r\n",
		 wpj->cfg->httpforward);
	io_write(cur,NULL,buf,-1);
	io_close(cur);
	return 0;
      }
    }

#ifdef HAVE_SSL
    /* if SSL on */
    if (wpj->cfg->ssl){
      /* in no	<stream or xml	*/
      if ((strstr(buf,"<stream") == NULL)&&(strstr(buf,"xml") == NULL)){
	/* SSL accept */
	cur->mh->read	= IO_SSL_READ;
	cur->mh->write	= IO_SSL_WRITE;
	cur->mh->accept = IO_SSL_ACCEPT;

	    if ( (*cur->mh->accept)(cur, NULL,NULL) != cur->fd){
	      log_alert("error","error in accepting SSL");
	      io_close(cur);
	      return -1;
	    }
	    wpj->clients_ssl++;
	  }
	}
#endif
	cur->accepted = 1;
  }


  /* how much can read */
  maxlen = KARMA_READ_MAX(cur->k.val);

  if (maxlen > CLIENT_SOCKET_BUFFER) maxlen = CLIENT_SOCKET_BUFFER;

  len = (*(cur->mh->read))(cur, buf, maxlen);

  /* if we had a bad read or karma end */
  if (len == 0){

	if (maxlen <= 0){
	  if (!cur->is_data){
	    _io_link_data(cur);
	    cur->is_data = 1; /* still data in socket */
	  }
	  return 0;
	}

	log_alert("error","error in reading");
	io_close(cur);
	return -1;  /* go */
  }
  else if(len < 0){
	if(errno != EWOULDBLOCK && errno != EINTR && errno != EAGAIN
#ifdef HAVE_SSL
	   && cur->error != EAGAIN
#endif
	   ){
	  /* kill this socket and move on */
	  log_alert("error","error in reading.");
	  io_close(cur);
	  return -1;  /* go */
	}

	if (cur->is_data){
	  _io_unlink_data(cur);
	  cur->is_data = 0; /* no more data */
	}
  }
  else{
	/* add to sockets with data */
	if (len < maxlen){
	  if (cur->is_data){
		_io_unlink_data(cur);
		cur->is_data = 0; /* no more data */
	  }
	}
	else{
	  if (!cur->is_data){
		_io_link_data(cur);
		cur->is_data = 1; /* still data in socket */
	  }
	}

	if (karma_check_lk(&cur->k,len)){
	  if (cur->k.val <= 0){
		log_alert("READ","socket from %s out of karma",cur->ip);
	  }
	}

	buf[len] = '\0';
	log_debug("IO read from socket %d: %s", cur->fd, buf);

	/* parse data */
	(*cur->mh->parser)(cur, buf, len);

	if (cur->state == state_CLOSE){
	  return -1;
	}
  }

  return 0;
}

/*
 * main select loop thread
 */
void * io_main(void *arg){
    io		cur,
		temp;
    int		i,
		nfds;
    time_t	time,
		iotime,
		datatime;
    int length;
    BOOL	iocheck = 0, /* ping */
		datacheck = 0;
    int		server_read_count; /* reads every DEF_READ_SERVER in client loop */
    int		free_size=-1;


    datatime = iotime = timeGetTimeSec(); /* get sec */

    log_debug("IO is starting up");

    /* main loop */
    while (1){
	/* if we are closing down, exit the loop */
	if (io__data->shutdown == 1)
		  break;

		/* wait */
		Sleep(1);

		/* get loop time */
		time = timeGetTimeSec();

		/* how often check sockets, that might have data 1s */
		if ( (time-datatime) > wpj->cfg->datacheck ){
		  //	  log_debug("data check...");
		  datatime = time;
		  datacheck = 1;
		}

		/* how often check ping */
		if ( (time-iotime) > wpj->cfg->iocheck ){
		  log_debug("ping check...");
		  iotime = time;
		  iocheck = 1;
		}

		/* ################# SERVER ######################## */
		/* if server */
		if (io__data->serverio){
		  cur = io__data->serverio;

		  /* if server needs to be closed */
		  if (cur->state == state_CLOSE){

			fdop(cur->fd, REMFD);

			_io_close_server(cur);

			/* set refresh flag */
			io__data->refresh_flag	= 1;
			io__data->refresh_start = time;
			continue;
		  }
		  else
			/* if init server socket */
			if ( cur->k.val == KARMA_DEF_INIT ){
			  io__data->refresh_flag = 0;

			  /* reset the karma to restore val */
			  cur->k.val = cur->k.restore;

			  /* and make sure that they are in the read set */
			  if ( fdop(cur->fd, ADDFD) < 0){
				log_alert("error","unable to add client to epoll set");
			  }

	      /* try to read from opened server socket */
			  if (handle_server_read()==0){
				cur->last_io = time;
				cur->ping_in_progress = 0;
			  }

			  log_debug("add server to dev");
			}
		  /* if server has data for us */
			else
			  if (cur->is_data){
				while (cur->is_data){
				  if (handle_server_read() == 0){
					cur->last_io = time;
					cur->ping_in_progress = 0;
				  }
				  else{
					break;
				  }
				}
			  }

		  /* ************ SERVER PING ********** */
		  if (time > cur->last_io + 20){
			/* check ping */

			if (cur->ping_in_progress == 1){
	      /* check bufor */
			  free_size=-1;
			  if (ioctl(cur->fd, TIOCOUTQ, &free_size) == 0){
				//free_size -> not send data
				log_debug("server ping	bufor %d last bufor %d",
						  free_size,cur->write_buffer_count);

				/* if socket bufor has changed */
				if ((free_size==0)||
				    (free_size != cur->write_buffer_count)){
				  cur->last_io = time;
				  cur->ping_in_progress = 0;
				  continue;
				}

				/* if we need to disconnect */
				if (time > cur->last_io + 30){
				  log_alert("pingserver",
					    "disconnect server buf %d last %d",
					    free_size,
					    cur->write_buffer_count);
				  io_close(cur);
				  continue;
				}

			  }
			  else{
				/* error ioctl */
				io_close(cur);
				continue;
			  }
			}
			/* no ping in progress */
			else{
			  /* if something to write in queue */
			  if (cur->queue){
				log_debug("ping server","send ping queue");
				i = _io_write_dump_server(cur,&length);
				if (i < 0){
				  io_close(cur);
				  continue;
				}
			  }
			  else{
		    /* send ping */
				log_debug("ping server","send ping spacja");
				write(cur->fd," ",1);
			  }

			  /* remember bufor size */
			  free_size = -1;
			  if (ioctl(cur->fd, TIOCOUTQ, &free_size) == 0){

				if (free_size > 0){
				  cur->write_buffer_count = free_size;
				  cur->ping_in_progress = 1;
				}
				else{
			  cur->last_io = time;
			  cur->ping_in_progress = 0;
				}
			  }
			  else{
				log_alert("ping_server","ioctl error");
				io_close(cur);
				continue;
			  }
			}

		  }/* server ping */

	}else
	  if (io__data->refresh_flag == 1){
	    /* 5 minut to refresh sessions */
	    if (time > io__data->refresh_start + wpj->cfg->reconnect_time){
		  for(cur = io__data->master__list; cur != NULL;){
			if (wpj->clients[cur->pollpos].state ==
				state_REFRESHING_SESSION){
			  /* close */
			  temp = cur;
			  cur = cur->next;
			  close_client(temp);
			  continue;
			}
			cur = cur->next;
		  }

		}
	  }
	/* serverio */


	/* ################# CLIENTS ######################## */
	/* loop all sockets, update ping */
	if (iocheck){
	  //	  log_alert("iocheck","time %d",time);

	  for(cur = io__data->master__list; cur != NULL;){
	    /* check socket that needs to be close */
	    if (cur->state == state_CLOSE){
	      log_debug("not closed socket");
	      temp = cur;
	      cur = cur->next;
	      close_client(temp);
	      continue;
	    }

	    /* check state */
	    /* not authed */
	    if (wpj->clients[cur->pollpos].state == state_UNKNOWN){
	      if (time > wpj->clients[cur->pollpos].time + wpj->cfg->auth_time){
		if (wpj->clients[cur->pollpos].orghost){
		  log_alert("auth","auth time exceeded %s",
			    jid_full(wpj->clients[cur->pollpos].orghost));
		}
		else{
		  log_alert("auth","auth time exceeded");
		}
		temp = cur;
		cur = cur->next;
		close_client(temp);
		continue;
	      }
	    }
	    else{
		  /* check timouted s_sended packets */
		  if (cur->s_queue){
			io_sended s_cur, s_temp;

			for(s_cur = cur->s_queue; s_cur != NULL;){

			  if (time > s_cur->t + wpj->cfg->ioclose + wpj->cfg->iocheck){
				log_debug("packet queue remove time %d now %d",
						  s_cur->t,
						  time);

				/* move the queue up */
				cur->s_queue = s_cur->next;

				/* set the tail pointer if needed */
				if(cur->s_queue == NULL)
				  cur->s_tail = NULL;

				s_temp = s_cur;
				s_cur = s_cur->next;

				/* if not older */
				xmlnode_free(s_temp->x);
				continue;
			  }
			  break;
			}//for s_cur
		  }

	      /* status AUTHED or REFRESH */
	      /* if time to ping, 3 minutes after lastio */
	      if (time > cur->last_io + wpj->cfg->ping_time){
			log_alert("ping","%p time to ping last io: %d",cur, cur->last_io);

			log_debug("ping check in socket");
			/* if ping was sent */
			if (cur->ping_in_progress == 1){
			  /* check bufor */
			  free_size=-1;
			  if (ioctl(cur->fd, TIOCOUTQ, &free_size) == 0){
				log_alert("pingcheck","%p check bufor %d my bufor %d",
						  cur, free_size, cur->write_buffer_count);

				log_debug("ping check in progress bufor %d my bufor %d",
						  free_size,cur->write_buffer_count);

				/* if socket bufor has changed */
				if ((free_size==0)||(free_size != cur->write_buffer_count)){
				  log_alert("ping","%p buffer has changed, reset ping",cur);
				  cur->last_io = time;
				  cur->ping_in_progress = 0;
				  continue;
				}

				/* if we need to disconnect */
				if (time > cur->last_io + wpj->cfg->ioclose){
				  log_alert("ping","%p disconnect last_io %d",cur,cur->last_io);
				  temp = cur;
				  cur = cur->next;
				  close_client(temp);
				  continue;
				}
			  }
			  else{
				log_alert("ioctl","error ioctl");
				temp = cur;
				cur = cur->next;
				close_client(temp);
				continue;
			  }
			}
			else{
			  cur->ping_in_progress = 1;
			  /* if something to write in queue */
			  if (cur->queue){
				log_alert("ping","%p send ping from queue",cur);
				_io_write_dump_client(cur);
			  }
			  else{
				/* send ping */
				log_debug("send ping spacja");
#ifdef HAVE_SSL
				if (cur->ssl){
				  log_alert("ping","%p send ping space SSL",cur);
				  io_write(cur,NULL," ",1);
				}
				else
#endif
				  {
					log_alert("ping","%p send ping space",cur);
					write(cur->fd," ",1);
				  }
			  }

			  /* remember bufor size */
			  free_size=-1;
			  if (ioctl(cur->fd, TIOCOUTQ, &free_size) == 0){

				if (free_size > 0){
				  cur->write_buffer_count = free_size;
				  cur->ping_in_progress = 1;
				}
				else{
				  cur->last_io = time;
				  cur->ping_in_progress = 0;
				}

				log_alert("ping","%p after send buffer :%d",cur, free_size);

				log_debug("bufor size %d",free_size);
			  }
			  else{
				log_alert("ioctl","error");
				temp = cur;
				cur = cur->next;
				close_client(temp);
				continue;
			  }
			}
	      }

	    }
	    cur = cur->next;
	  }
	}

	if (iocheck == 1) iocheck = 0;


	/* loop sockets that might hava data */
	/* here are socket - out of karma in 90% */
	/* check once 1 sek */
	if (datacheck){

	  /* if not overloded check sockets */
	  if (!wpj->overload)
		for (cur = io__data->data__list; cur != NULL;){
		  log_debug("socket %d in data__list",cur->fd);
		  if (karma_increment_lk(&cur->k,time)){ /* if karma > 0 */
			if (handle_client_read(cur) == -1){
			  temp = cur;
			  cur = cur->next;
			  close_client(temp);
			  continue;
			}
			else{
			  cur->last_io = time;
			  cur->ping_in_progress = 0;
			}
		  }
		  cur = cur->next;
		}
	}

	if (datacheck == 1) datacheck = 0;

	/* ################# ACTIVE READ ######################## */
	/* epoll */
	nfds = epoll_wait(io__data->epollfd, io__data->evp,
			  wpj->cfg->epoll_maxevents, 0);

	/* check sockets */
	server_read_count = 0; /* how often check server for read */
	for (i = 0; i < nfds; i++){

	  /* check if we need to read from server */
	  if ((io__data->serverio)&&(io__data->evp[i].data.fd == io__data->serverio->fd )){
	    if (io__data->evp[i].events & EPOLLIN){
	      /* read from server */
	      server_read_count = 0;

	      if (handle_server_read() == 0){
		io__data->serverio->last_io = time;
		io__data->serverio->ping_in_progress =0;
	      }
	    }
	    else
	      /* error server */
	      if (io__data->evp[i].events & (EPOLLHUP|EPOLLERR)){
			io_close(io__data->serverio);
	      }/* error server */
	  }
	  else
	    if ((io__data->listenio)&&(io__data->evp[i].data.fd == io__data->listenio->fd )){
	      /* accept new connection */
	      cur = NULL;
	      while (cur != ((void *)-1)){

		cur = _io_accept(io__data->listenio);

		if (cur != ((void *)-1)){/* if more sockets */
		  if (cur != NULL){ /* if accepted */
				/* set karma */
		    cur->k.val = cur->k.restore;
		    log_debug("accepted socket karma restore =>%d",cur->k.restore);

				/* add to epoll set */
		    fdop(cur->fd,ADDFD);
		    add_client(cur->fd,cur);

				/* update last io time */
		    cur->last_io = time;
		    cur->ping_in_progress = 0;

				/* time of connection */
		    wpj->clients[cur->pollpos].time = time;

				/* try to read */
		    if (wpj->overload == 2){
		      _io_link_data(cur);
		      cur->is_data = 1; /* still data in socket */
		    }
		    else
		      if (handle_client_read(cur) == -1){
			log_debug("socket close");
			temp = cur;
			close_client(temp);
			continue;
		      }

		    log_debug("accepted socket");
		  } else{
		    log_alert("errro","didn't accept socket");
		  }
		}
	      }
	    }
	    else{
	      server_read_count++;
	      if (server_read_count == DEF_READ_SERVER){
			server_read_count = 0;
			if (handle_server_read() == 0){
			  io__data->serverio->last_io = time;
			  io__data->serverio->ping_in_progress =0;
			}
	      }

	      /* find cur in client hash */
	      cur = find_client(io__data->evp[i].data.fd);

	      if (cur == NULL) continue; /* go */

	      if (cur->state == state_CLOSE){
			temp = cur;
			close_client(temp);
			continue;
	      }

		  /* error */
		  if (io__data->evp[i].events & (EPOLLHUP|EPOLLERR)){
			log_alert("error","socket close - HUP,ERR");
			io_close(cur);
			temp = cur;
			close_client(temp);
			continue;
		  }

	      /* read event */
	      if (io__data->evp[i].events & EPOLLIN){
			/* can read */
			/* update last socket reaction */
			cur->last_io = time;
			if (cur->ping_in_progress){
			  cur->ping_in_progress = 0;
			}

			/* do not read if overload */
			if (wpj->overload == 2){
			  if (!cur->is_data){
				_io_link_data(cur);
				cur->is_data = 1;
			  }
			  continue;
			}

			if (cur->k.val <= 0){
			  if (!cur->is_data){
				_io_link_data(cur);
				cur->is_data = 1;
			  }
			  continue;
			}

			karma_increment_lk(&cur->k,time);

			log_debug("event read karma %d",cur->k.val);

			if (handle_client_read(cur) == -1){
			  temp = cur;
			  close_client(temp);
			  continue;
			}

	      }/*read*/
	      else
		/* write */
		if (io__data->evp[i].events & EPOLLOUT){
		  if (cur->queue != NULL){ /* if something to write */
		    int ret;
		    /* write the current buffer */
		    ret = _io_write_dump_client(cur);
		    if (ret == -1){ /* error */
		      io_close(cur);
		      temp = cur;
		      close_client(temp);
		      continue;
		    }
		  }
		}/* write */
	    }
	}/*for*/

    }/* while(1) end */

    return 0;

}

/***************************************************\
*      E X T E R N A L	 F U N C T I O N S	    *
\***************************************************/

/*
   starts the io_main() loop
*/
void io_init(void){
  int i;

    if(io__data == NULL){
	/* malloc our instance object */
	io__data	      = pmalloco(wpj->p, sizeof(_ios));
	io__data->karma_time  = wpj->cfg->karma_time;

	io__data->listenio    = NULL;
	io__data->serverio    = NULL;

	for (i = 0 ; i < CLIENT_HASH_SIZE ; i++)
	  client_hash[i] = NULL;

	/* init rate */
	RateInit(DEF_RATE_CONN,DEF_RATE_TIME,DEF_CONN);


	/* init epoll fd and events array */
	io__data->evp = NULL;
	io__data->epollfd = epoll_create(wpj->cfg->epoll_size);

	if (io__data->epollfd != -1){
	  log_debug("epoll fd created: %d (size=%d)", io__data->epollfd, wpj->cfg->epoll_size);
	  io__data->evp = pmalloco(wpj->p, (wpj->cfg->epoll_maxevents+1)*
				   sizeof(struct epoll_event));
	}
	else{
	  log_alert("epoll_error","Can't epoll_create(%d). Read README for more info", wpj->cfg->epoll_size);
	}

	/* init working threads */
	pthread_create(&io__data->t, NULL, io_main, NULL);
	pthread_create(&io__data->wt, NULL, write_queue_main, NULL);
    }
}

/*
 * Cleanup function when server is shutting down, closes
 * all sockets, so that everything can be cleaned up
 * properly.
 */
void io_stop(void){
    io cur,temp;
    void * ret;

    log_alert("stop", "IO is shutting down");

    /* no need to do anything if io__data hasn't been used yet */
    if(io__data == NULL)
	return;

    /* flag that it is okay to exit the loop */
    io__data->shutdown = 1;
    pthread_join(io__data->t,&ret);

    /* close listen socket */
    if (io__data->listenio)
      _io_close_listen(io__data->listenio);

    io__data->listenio = NULL;

    /* loop client socket, and close it */
    for(cur = io__data->master__list; cur != NULL; ){
      log_alert("stop","closing client sockets. data %d",cur->queue_long);
      temp = cur;
      cur = cur->next;
      close_client(temp);
    }


    log_alert("stop","IO stop: stopped clients");

	/* wait until all server data are written to server */
	if (io__data->serverio){
	  /* while data and active connection */
	  while (io__data->serverio->queue && io__data->serverio->state == state_ACTIVE){
		Sleep(100);
	  }
	}

	/* end server writing thread */
	io__data->shutdown = 3;
	pthread_join(io__data->wt,&ret);

	/* end connection to server */
	if (io__data->serverio)
	  _io_close_server(io__data->serverio);

    io__data->serverio = NULL;

    /* stop rate */
    RateStop();

    /* close epoll fd */
    close(io__data->epollfd);

    log_alert("stop","IO stop end");

    io__data = NULL;
}

/*
   creates a new io object from a file descriptor
*/
io io_new(int fd, void *cb, void *arg){
    io	  new	 =  NULL;
    pool  p	 =  NULL;
    int   flags  =  0;

    if(fd <= 0)
	return NULL;

    /* create the new IO object */
    p		= pool_heap(512);
    new		= pmalloco(p, sizeof(_io));
    new->p	= p;
    new->type	= type_NORMAL;
    new->state	= state_ACTIVE;
    new->fd	= fd;
    new->cb	= (void*)cb;
    new->cb_arg = arg;
    new->pollpos= -1;

    /* set the default karma values */
    io_karma(new, KARMA_DEF_INIT, KARMA_DEF_MAX, KARMA_DEF_INC, KARMA_DEF_DEC, KARMA_DEF_PENALTY, KARMA_DEF_RESTORE);


    /* set the socket to non-blocking */
    flags =  fcntl(fd, F_GETFL, 0);
    flags |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flags);

    /* add to the select loop */
    _io_link(new);

    return new;
}

/*
 * client call to close the socket
 */
void io_close(io m){
    if(m == NULL)
	return;

    m->state = state_CLOSE;
}

/*
 * writes a str, or xmlnode to the client socket
 */
void io_write(io m, xmlnode x, char *buffer, int len){
    io_wbq new;
    pool p;

    log_debug("io_write called on x: %X buffer: %.*s", x, len, buffer);

    if(m == NULL){
      if (x != NULL)
	xmlnode_free(x);
      log_alert("write","Writing to non existing socket !!!");
      return;
    }

    /* if there is nothing to write */
    if(x == NULL && buffer == NULL)
	return;

    if ( m->type == type_SERVER ){
      pthread_mutex_lock(&(m->sem));
	}

    /* create the pool for this wbq */
    if(x != NULL)
	  p = xmlnode_pool(x);
    else
	  p = pool_new();

    /* create the wbq */
    new    = pmalloco(p, sizeof(_io_wbq));
    new->p = p;

    /* set the queue item type */
    if(buffer != NULL){
	  new->type = queue_CDATA;

	  if (len == -1)
		len = strlen(buffer);

	  /* XXX more hackish code to print the stream header right on a NUL xmlnode socket */
	  if(m->type == type_NUL && strncmp(buffer,"<stream:stream",14)){
		  len++;
		  new->data = pmalloco(p,len+1);
		  snprintf(new->data,len+1,"%.*s/>",len-2,buffer);
	  }
	  else{
		new->data = pmalloco(p,len);
		memcpy(new->data,buffer,len);
	  }
    }
    else{
	  new->type = queue_XMLNODE;
	  new->data = xmlnode2str(x);
	  len = new->data ? strlen(new->data) : 0;
    }

    /* include the \0 if we're special */
    if(m->type == type_NUL){
	  len++;
    }

    /* assign values */
    new->x    = x;
    new->cur  = new->data;

    new->len = len;

    /* put at end of queue */
    if(m->tail == NULL)
	m->queue = new;
    else
	m->tail->next = new;
    m->tail = new;

    m->queue_long++;

    /*----- only for server ----*/
    if ( m->type == type_SERVER ){
      if (!wpj->overload){
		if (m->queue_long > OVERLOAD_NORMAL){
		  wpj->overload = 1;
		}
      }
      else
		if (wpj->overload == 1){
		  if (m->queue_long > OVERLOAD_CRITICAL){
			wpj->overload = 2;
			log_alert("--","2");
		  }
		}
      pthread_mutex_unlock(&(m->sem));
    }
    else{
	  int ret;
	  /* client - always try to write , optymize this !!!! */
      ret = _io_write_dump_client(m);
      if (ret == -1){	  /* error */
		io_close(m);
      }
    }
}

/*
  sets karma values
*/
void io_karma(io m, int val, int max, int inc, int dec, int penalty, int restore){
    if(m == NULL)
       return;

    m->k.val	 = val;
    m->k.max	 = max;
    m->k.inc	 = inc;
    m->k.dec	 = dec;
    m->k.penalty = penalty;
    m->k.restore = restore;
}

void io_karma2(io m, struct karma *k){
    if(m == NULL)
       return;

    m->k.val	 = k->val;
    m->k.max	 = k->max;
    m->k.inc	 = k->inc;
    m->k.dec	 = k->dec;
    m->k.penalty = k->penalty;
    m->k.restore = k->restore;
}


void io_return_packet(xmlnode x){
  if (wpj->cfg->si->state == conn_AUTHD){
	log_alert("return","return packet %s",xmlnode2str(x));
	io_write(wpj->si->m, x, NULL,-1);
  }
  else{
	log_alert("return","couldn't return packet %s, server down",xmlnode2str(x));
	xmlnode_free(x);
  }
}

/*
   pops the last xmlnode from the queue
*/
void io_cleanup(io m){
    io_wbq     cur,temp;
    io_sended	  s_cur,s_temp;
	int free_size = -1;

    if(m == NULL)
	  return;

    if (m->type == type_SERVER ){
	  if (m->queue==NULL)
		return;

      pthread_mutex_lock(&(m->sem));

	  for(cur = m->queue; cur != NULL;){
	m->queue = cur->next;

	if(m->queue == NULL)
		  m->tail = NULL;

		temp = cur;
		cur = cur->next;

		pool_free(temp->p);
	  }


      pthread_mutex_unlock(&(m->sem));
	  return;
	}

	/* clear sended packets */
	if (m->s_queue){
	  if (ioctl(m->fd, TIOCOUTQ, &free_size) != 0){
		free_size = -1;
	  }

	  for(s_cur = m->s_queue; s_cur != NULL;){
		/* move the queue up */
		m->s_queue = s_cur->next;

		/* set the tail pointer if needed */
		if(m->s_queue == NULL)
		  m->s_tail = NULL;

		s_temp = s_cur;
		s_cur = s_cur->next;

		/* if not older */
		if (free_size > 0){
		  io_return_packet(s_temp->x);
		}
		else{
		  xmlnode_free(s_temp->x);
		}
	  }
	}

	/* clear socket unsended packets */
    for(cur = m->queue; cur != NULL;){
	/* move the queue up */
	m->queue = cur->next;

	/* set the tail pointer if needed */
	if(m->queue == NULL)
	    m->tail = NULL;

		temp = cur;
		cur = cur->next;

		/* if cur->x and message */
		if ((wpj->cfg->no_message_storage==0) &&
			(temp->x)&&
			(strncmp(xmlnode_get_name(temp->x),"message",7) == 0)){
		  io_return_packet(temp->x);
		}
		else{
		  pool_free(temp->p);
		}
    }

    return;
}

/*
 * request to connect to a remote host
 */

void io_connect(char *host, int port, void *cb, void *cb_arg, int timeout){
    connect_data cd = NULL;
    pool	 p  = NULL;
    pthread_attr_t attr;


    if(host == NULL || port == 0)
      return;

    if (io__data->shutdown > 0)
      return;

    p		= pool_new();
    cd		= pmalloco(p, sizeof(_connect_data));
    cd->p	= p;
    cd->ip	= pstrdup(p, host);
    cd->port	= port;
    cd->cb	= cb;
    cd->cb_arg	= cb_arg;
    cd->timeout = timeout;

    /* set detach able */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
    pthread_create(&cd->t, &attr, _io_connect,	(void*)cd);
    pthread_attr_destroy(&attr);
}


/*
 * call to start listening with select
 */
io io_listen(int port, char *listen_host, void *cb, void *arg){
    io	       new;
    int        fd;
    pool       p      =  NULL;
    int        flags  =  0;

    if (io__data->shutdown > 0)
      return NULL;

    if (io__data->listenio)
      return NULL;

    log_debug("io_select to listen on %d [%s]",port, listen_host);

    /* attempt to open a listening socket */
    fd = make_netsocket(port, listen_host, NETSOCKET_SERVER);

    /* if we got a bad fd we can't listen */
    if(fd < 0){
      log_alert("create", "io_select unable to create listen socket on %d [%s] - %s", port, listen_host, strerror(errno));
      return NULL;
    }

    /* start listening with a max accept queue of 10 */
    if(listen(fd, 10) < 0){
      log_alert("listen", "io_select unable to listen on %d [%s] - %s", port, listen_host, strerror(errno));
      wpj_shutdown = 1;
      return NULL;
    }

    /* create the new IO object */
    p		= pool_heap(512);
    new		= pmalloco(p, sizeof(_io));
    new->p	= p;
    new->type	= type_LISTEN;
    new->state	= state_ACTIVE;
    new->fd	= fd;
    new->cb	= cb;
    new->cb_arg = arg;
    new->ip	= pstrdup(new->p, listen_host);
    new->pollpos= 0;

    new->mh = pmalloco(p, sizeof(_io_handlers));
    new->mh->read   = IO_RAW_READ;
    new->mh->write  = IO_RAW_WRITE;
    new->mh->parser = IO_XML_PARSER;
    new->mh->accept = IO_RAW_ACCEPT;

    /* set the default karma values */
    io_karma(new, KARMA_DEF_INIT, KARMA_DEF_MAX, KARMA_DEF_INC, KARMA_DEF_DEC, KARMA_DEF_PENALTY, KARMA_DEF_RESTORE);

    /* set the socket to non-blocking */
    flags =  fcntl(fd, F_GETFL, 0);
    flags |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flags);

    /* listenio */
    io__data->listenio = new;

    /* add to epoll set */
    if ( fdop(fd, ADDFD) < 0){
      _io_close_listen(new);
      log_alert("epoll","unable to add listen to epoll set");
      new = NULL;
    }

    log_debug("io_select starting to listen on %d [%s]", port, listen_host);

    return new;
}
