/* --------------------------------------------------------------------------
 *
 *  WP
 *
 * --------------------------------------------------------------------------*/

#include "wpj.h"

void _io_raw_parser(io m, const void *buf, size_t bufsz){
    (*(io_raw_cb)m->cb)(m, IO_BUFFER, m->cb_arg, (char*)buf, bufsz);
}

ssize_t _io_raw_read(io m, void *buf, size_t count){
    return IO_READ_FUNC(m->fd, buf, count);
}

ssize_t _io_raw_write(io m, void *buf, size_t count){
    return IO_WRITE_FUNC(m->fd, buf, count);
}

int _io_raw_accept(io m, struct sockaddr* serv_addr, socklen_t* addrlen){
  //   return IO_ACCEPT_FUNC(m->fd, serv_addr, addrlen);
  return 0;
}

int _io_raw_connect(io m, struct sockaddr* serv_addr, socklen_t  addrlen){
   return IO_CONNECT_FUNC(m->fd, serv_addr, addrlen);
}









