#include "jabberd.h"

#ifdef HAVE_SSL
HASHTABLE ssl__ctxs;


#ifndef NO_RSA
/* This function will generate a temporary key for us */
RSA *_ssl_tmp_rsa_cb(SSL * ssl, int export, int keylength)
{
	RSA *rsa_tmp = NULL;

	rsa_tmp = RSA_generate_key(keylength, RSA_F4, NULL, NULL);
	if (!rsa_tmp) {
		log_debug("Error generating temp RSA key");
		return NULL;
	}

	return rsa_tmp;
}
#endif				/* NO_RSA */

/***************************************************************************
 * This can return whatever we need, it is just designed to read a xmlnode
 * and hash the SSL contexts it creates from the keys in the node
 *
 * Sample node:
 * <ssl>
 *   <key ip='192.168.1.100'>/path/to/the/key/file.pem</key>
 *   <key ip='192.168.1.1'>/path/to/the/key/file.pem</key>
 * </ssl>
 **************************************************************************/
void mio_ssl_init(xmlnode x)
{
/* PSEUDO CODE

  for $key in children(xmlnode x){
      - SSL init
      - Load key into SSL ctx
      - Hash ctx based on hostname
  }

  register a cleanup function to free our contexts
*/

	SSL_CTX *ctx = NULL;
	xmlnode cur;
	char *host;
	char *keypath;

	log_debug("MIO SSL init");

	/* Make sure we have a valid xmlnode to play with */
	if (x == NULL && xmlnode_has_children(x)) {
		log_debug("SSL Init called with invalid xmlnode");
		return;
	}

	log_debug("Handling configuration using: %s", xmlnode2str(x));
	/* Generic SSL Inits */
	OpenSSL_add_all_algorithms();
	SSL_load_error_strings();

	/* Setup our hashtable */
	ssl__ctxs = ghash_create(19, (KEYHASHFUNC) str_hash_code,
				 (KEYCOMPAREFUNC) j_strcmp);

	/* Walk our node and add the created contexts */
	for (cur = xmlnode_get_tag(x, "key"); cur != NULL;
	     cur = xmlnode_get_nextsibling(cur)) {
		host = xmlnode_get_attrib(cur, "ip");
		keypath = xmlnode_get_data(cur);

		if (!host || !keypath)
			continue;

		log_debug("Handling: %s", xmlnode2str(cur));

		ctx = SSL_CTX_new(SSLv23_server_method());
		if (ctx == NULL) {
			unsigned long e;
			static char *buf;

			e = ERR_get_error();
			buf = ERR_error_string(e, NULL);
			log_debug("Could not create SSL Context: %s", buf);
			return;
		}
#ifndef NO_RSA
		log_debug("Setting temporary RSA callback");
		SSL_CTX_set_tmp_rsa_callback(ctx, _ssl_tmp_rsa_cb);
#endif				/* NO_RSA */

		SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_BOTH);

		/* XXX I would like to make this a configurable option */
		/*
		   SSL_CTX_set_timeout(ctx, session_timeout);
		 */

		/* Setup the keys and certs */
		log_debug("Loading SSL certificate %s for %s", keypath,
			  host);
		if (!SSL_CTX_use_certificate_file
		    (ctx, keypath, SSL_FILETYPE_PEM)) {
			log_debug("SSL Error using certificate file");
			SSL_CTX_free(ctx);
			continue;
		}
		if (!SSL_CTX_use_PrivateKey_file
		    (ctx, keypath, SSL_FILETYPE_PEM)) {
			log_debug("SSL Error using Private Key file");
			SSL_CTX_free(ctx);
			continue;
		}
		ghash_put(ssl__ctxs, host, ctx);
		log_debug("Added context %x for %s", ctx, host);
	}

}

void _mio_ssl_cleanup(void *arg)
{
	SSL *ssl = (SSL *) arg;

	log_debug("SSL Cleanup for %x", ssl);
	SSL_free(ssl);
}

ssize_t _mio_ssl_read(mio m, void *buf, size_t count)
{
	return SSL_read((SSL *) m->ssl, (char *) buf, count);
}

ssize_t _mio_ssl_write(mio m, const void *buf, size_t count)
{
	return SSL_write((SSL *) m->ssl, buf, count);
}

int _mio_ssl_accept(mio m, struct sockaddr *serv_addr, socklen_t * addrlen)
{
	SSL *ssl = NULL;
	SSL_CTX *ctx = NULL;
	int fd;

	if (m->ip == NULL) {
		log_warn("ssl", "SSL accept without an IP");
		return -1;
	}

	fd = accept(m->fd, serv_addr, addrlen);

	ctx = ghash_get(ssl__ctxs, m->ip);
	if (ctx == NULL) {
		log_debug("NULL ctx found in SSL hash");
		return -1;
	}
	ssl = SSL_new(ctx);
	log_debug("SSL accepting socket with new session %x", ssl);
	SSL_set_fd(ssl, fd);
	SSL_set_accept_state(ssl);
	if (SSL_accept(ssl) <= 0) {
		unsigned long e;
		static char *buf;

		e = ERR_get_error();
		buf = ERR_error_string(e, NULL);
		log_debug("Error from SSL: %s", buf);
		log_debug("SSL Error in SSL_accept call");
		SSL_free(ssl);
		close(fd);
		return -1;
	}

	m->ssl = ssl;

	log_debug("Accepted new SSL socket %d for %s", fd, m->ip);

	return fd;
}

int _mio_ssl_connect(mio m, struct sockaddr *serv_addr, socklen_t addrlen)
{

	/* PSEUDO
	   I need to actually look this one up, but I assume it's similar to the
	   SSL accept stuff.
	 */
	SSL *ssl = NULL;
	SSL_CTX *ctx = NULL;
	int fd;

	log_debug("Connecting new SSL socket for %s", m->ip);
	ctx = ghash_get(ssl__ctxs, m->ip);

	fd = connect(m->fd, serv_addr, addrlen);
	SSL_set_fd(ssl, fd);
	if (SSL_connect(ssl) <= 0) {
		log_debug("SSL Error in SSL_connect call");
		SSL_free(ssl);
		close(fd);
		return -1;
	}

	pool_cleanup(m->p, _mio_ssl_cleanup, (void *) ssl);

	m->ssl = ssl;

	return fd;
}

#endif				/* HAVE_SSL */
