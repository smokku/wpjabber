

#include "wpj.h"


#ifdef HAVE_SSL

#include <openssl/err.h>
#include <openssl/ssl.h>


SSL_CTX *ssl__ctx;

#ifndef NO_RSA
static RSA *rsa512=NULL;
static RSA *rsa1024=NULL;
static void compute_temprary_rsa_keys(){

    if ((rsa512 = RSA_generate_key(512, RSA_F4, NULL, NULL)) == NULL){
	log_alert("ssl_error","SSL:Failed to generate temporary 512 bit RSA private key");
    }

    if ((rsa1024 = RSA_generate_key(1024, RSA_F4, NULL, NULL)) == NULL){
	log_alert("ssl_error","SSL:Failed to generate temporary 1024 bit RSA private key");
    }
}
static void free_temprary_rsa_keys(){
    if (rsa512){
	RSA_free(rsa512);
	rsa512=NULL;
    }
    if (rsa1024){
	RSA_free(rsa1024);
	rsa1024=NULL;
    }
}

RSA *_ssl_tmp_rsa_cb(SSL *ssl, int export, int keylength){
    if (export){
	/* It's because an export cipher is used */
	if (keylength == 512) return rsa512;
	else if (keylength == 1024) return rsa1024;
	else return rsa1024;
    } else{
	/* It's because a sign-only certificate
	 * situation exists */
	return rsa1024;
    }
    return NULL;
}

#endif /* NO_RSA */

/***************************************************************************
 * Sample node:
 * <ssl>
 *   <key ip='192.168.1.100'>/path/to/the/key/file.pem</key>
 * </ssl>
 **************************************************************************/
void io_ssl_stop(){
  log_debug("Stop SSL");

  if (ssl__ctx)
    SSL_CTX_free(ssl__ctx);

  free_temprary_rsa_keys();

  ERR_free_strings();
  ERR_remove_state(0);
  EVP_cleanup();
}

int io_ssl_init(xmlnode x){
    SSL_CTX *ctx = NULL;
    xmlnode cur;
    char *host;
    char *keypath;

    log_debug("IO SSL init");

    /* Make sure we have a valid xmlnode to play with */
    if(x == NULL && xmlnode_has_children(x)){
	log_debug("SSL Init called with invalid xmlnode");
	return 1;
    }

    log_debug("Handling configuration using: %s", xmlnode2str(x));

    /* Generic SSL Inits */
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();

    compute_temprary_rsa_keys();

    /* Walk our node and add the created contexts */
    cur = xmlnode_get_tag(x, "key");
	if (!cur){
		  log_alert("ssl", "no key");
		  return 1;
	}

	host = xmlnode_get_attrib(cur, "ip");
	keypath = xmlnode_get_data(cur);

	if(!host || !keypath){
	  log_alert("ssl", "no keypath or IP");
	  return 1;
	}

	log_debug("Handling: %s", xmlnode2str(cur));

	ctx=SSL_CTX_new(SSLv23_server_method());
	if(ctx == NULL){
		unsigned long e;
		static char *buf;

		e = ERR_get_error();
		buf = ERR_error_string(e, NULL);
		log_alert("ssl", "Could not create SSL Context: %s", buf);
		return 1;
	  }

#ifndef NO_RSA
	log_debug("Setting temporary RSA callback");
	SSL_CTX_set_tmp_rsa_callback(ctx, _ssl_tmp_rsa_cb);
#endif /* NO_RSA */

	SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_BOTH);

	/* Setup the keys and certs */
	log_debug("Loading SSL certificate %s for %s", keypath, host);
	if(!SSL_CTX_use_certificate_file(ctx, keypath,SSL_FILETYPE_PEM)){
		log_alert("ssl", "SSL Error using certificate file");
		SSL_CTX_free(ctx);
		return 1;
	  }
	if(!SSL_CTX_use_PrivateKey_file(ctx, keypath,SSL_FILETYPE_PEM)){
		log_alert("ssl", "SSL Error using Private Key file");
		SSL_CTX_free(ctx);
		return 1;
	  }

	if (xmlnode_get_tag_data(x, "ciphers")){
	  if (SSL_CTX_set_cipher_list(ctx,xmlnode_get_tag_data(x, "ciphers"))){
		SSL *ssl;
		int i;
		STACK_OF(SSL_CIPHER) *sk;
		char buf[512];

		log_alert("config","using ciphers: >%s<",xmlnode_get_tag_data(x, "ciphers"));

		ssl = SSL_new(ctx);
		if (ssl == NULL){
		  log_alert("config","can't create ssl from ctx");
		  SSL_CTX_free(ctx);
		  return 1;
		}

		sk = SSL_get_ciphers(ssl);
		for (i=0; i < sk_SSL_CIPHER_num(sk); i++){
		  SSL_CIPHER_description(sk_SSL_CIPHER_value(sk,i),
								 buf,
								 sizeof buf);

		  log_alert("cipher","%s",buf);
		}
		SSL_free(ssl);
	  }
	  else
		log_alert("config","error using ciphers: >%s<",xmlnode_get_tag_data(x, "ciphers"));
	}

	ssl__ctx =  ctx;
	log_debug("Loaded context %x for %s", ctx, host);

	return 0;
}

void _io_ssl_cleanup(void *arg){
    SSL *ssl = (SSL *)arg;

    log_debug("SSL Cleanup for %x", ssl);
    SSL_free(ssl);
}

ssize_t _io_ssl_read(io m, void *buf, size_t count){
    SSL *ssl;
	int count_ret;
    ssize_t ret;
    int sret;

    ssl = m->ssl;

    m->error = 0;

    if(count <= 0)
	return 0;

    log_debug("Asked to read %d bytes from %d", count, m->fd);

    if(SSL_get_state(ssl) != SSL_ST_OK){
	sret = SSL_accept(ssl);
	if(sret <= 0){
	    unsigned long e;
	    static char *buf;

	    if((SSL_get_error(ssl, sret) == SSL_ERROR_WANT_READ) ||
	       SSL_get_error(ssl, sret) == SSL_ERROR_WANT_WRITE){
		log_debug("Read blocked, returning");

		m->error = EAGAIN;
		return -1;
	    }
	    e = ERR_get_error();
	    buf = ERR_error_string(e, NULL);
	    log_alert("ssl", "Error from SSL: %s", buf);
	    //		  close(m->fd);
	    return -1;
	}
    }
	count_ret = 0;
    do {
	  ret = SSL_read(ssl, (char *)buf, count);
	  if (ret > 0){
		count_ret += ret;
		buf += ret;
		count -= ret;
	  }
    } while (count>0 && ret>0);

	if ((count_ret==0)&&(ret==-1)){
	  int err;

	  count_ret = -1;

	  err = SSL_get_error(ssl, ret);

	  if ((err == SSL_ERROR_WANT_READ) ||
		  (err == SSL_ERROR_WANT_WRITE)){
		log_debug("Read blocked, returning.");
		m->error = EAGAIN;
		return -1;
	  }

	  if (err == 1){
		char buf[121];
		while ((err = ERR_get_error())>0){
		  ERR_error_string(err,buf);
		  log_alert("ssl", "err %d %s",err,buf);
		}
	  }
	}

	log_debug("SSL read count %d from %d",count_ret,count);

    return count_ret;
}

ssize_t _io_ssl_write(io m, const void *buf, size_t count){
    SSL *ssl;
    ssl = m->ssl;
    m->error = 0;

    if(SSL_get_state(ssl) != SSL_ST_OK){
	int sret;

	sret = SSL_accept(ssl);
	if(sret <= 0){
	    unsigned long e;
	    static char *buf;

	    if((SSL_get_error(ssl, sret) == SSL_ERROR_WANT_READ) ||
	       SSL_get_error(ssl, sret) == SSL_ERROR_WANT_WRITE){
		log_debug("Write blocked, returning");

		m->error = EAGAIN;
		return -1;
	    }
	    e = ERR_get_error();
	    buf = ERR_error_string(e, NULL);
	    log_alert("ssl", "Error from SSL: %s", buf);
	    //		  close(m->fd);
	    return -1;
	}
    }
    return SSL_write(ssl, buf, count);
}

int _io_ssl_accept(io m, struct sockaddr *serv_addr, socklen_t *addrlen){
    SSL *ssl=NULL;
    int sret;

    ssl = SSL_new(ssl__ctx);

    log_debug("SSL accepting socket with new session %x", ssl);

    SSL_set_fd(ssl, m->fd);
    SSL_set_accept_state(ssl);
    sret = SSL_accept(ssl);
    if(sret <= 0){
	unsigned long e;
	static char *buf;

	if((SSL_get_error(ssl, sret) == SSL_ERROR_WANT_READ) ||
	   (SSL_get_error(ssl, sret) == SSL_ERROR_WANT_WRITE)){
	    m->ssl = ssl;
	    pool_cleanup(m->p, _io_ssl_cleanup, (void *)m->ssl);
	    log_debug("Accept blocked, returning");
	    return m->fd;
	}
	e = ERR_get_error();
	buf = ERR_error_string(e, NULL);
	log_alert("ssl", "Error from SSL: %s", buf);
	SSL_free(ssl);
	//	  close(m->fd);
	return -1;
    }

    m->k.val = 100;
    m->ssl = ssl;
    pool_cleanup(m->p, _io_ssl_cleanup, (void *)m->ssl);

    log_debug("Accepted new SSL socket %d for %s", m->fd, m->ip);

    return m->fd;
}

#endif /* HAVE_SSL */



