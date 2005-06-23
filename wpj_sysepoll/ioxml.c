/* --------------------------------------------------------------------------
 *
 *  WP
 *
 * --------------------------------------------------------------------------*/

#include "wpj.h"

void _io_xstream_startElement(io m, const char* name, const char** attribs){
    /* If stacknode is NULL, we are starting a new packet and must
       setup for by pre-allocating some memory */
    if (m->stacknode == NULL){
	    pool p = pool_heap(5 * 1024); /* 5k, typically 1-2k each, plus copy of self and workspace */
	    m->stacknode = xmlnode_new_tag_pool(p, name);
	    xmlnode_put_expat_attribs(m->stacknode, attribs);

	    /* If the root is 0, this must be the root node.. */
	    if (m->root == 0){
	    if(m->cb != NULL)
		    (*(io_xml_cb)m->cb)(m, IO_XML_ROOT, m->cb_arg, m->stacknode);
	    else
		xmlnode_free(m->stacknode);
		m->stacknode = NULL;
	    m->root = 1;
	    }
    }
    else{
	    m->stacknode = xmlnode_insert_tag(m->stacknode, name);
	    xmlnode_put_expat_attribs(m->stacknode, attribs);
    }
}

void _io_xstream_endElement(io m, const char* name){
    /* If the stacknode is already NULL, then this closing element
       must be the closing ROOT tag, so notify and exit */
    if (m->stacknode == NULL){
	io_close(m);
    }
    else{
	    xmlnode parent = xmlnode_get_parent(m->stacknode);

	    /* Fire the NODE event if this closing element has no parent */
	    if (parent == NULL){
	    if(m->cb != NULL)
		    (*(io_xml_cb)m->cb)(m, IO_XML_NODE, m->cb_arg, m->stacknode);
	    else
		xmlnode_free(m->stacknode);
	    }
	    m->stacknode = parent;
    }
}

void _io_xstream_CDATA(io m, const char* cdata, int len){
    if (m->stacknode != NULL)
	    xmlnode_insert_cdata(m->stacknode, cdata, len);
}

void _io_xstream_cleanup(void* arg){
    io m = (void*)arg;

    xmlnode_free(m->stacknode);
    XML_ParserFree(m->parser);
    m->parser = NULL;
}

void _io_xstream_init(io m){
    if (m != NULL){
	    log_debug("_io_xstream_init(%p)",m);   /* sielim PATCH ?del */

	    /* Initialize the parser */
	    m->parser = XML_ParserCreate(NULL);
	    XML_SetUserData(m->parser, m);
	    XML_SetElementHandler(m->parser, (void*)_io_xstream_startElement, (void*)_io_xstream_endElement);
	    XML_SetCharacterDataHandler(m->parser, (void*)_io_xstream_CDATA);
	    /* Setup a cleanup routine to release the parser when everything is done */
	    pool_cleanup(m->p, _io_xstream_cleanup, (void*)m);
    }
}

/* this function is called when a socket reads data */
void _io_xml_parser(io m, const void *vbuf, size_t bufsz){
    char *nul, *buf = (char*)vbuf;

    /* init the parser if this is the first read call */
    if(m->parser == NULL){
	_io_xstream_init(m);
	/* XXX pretty big hack here, if the initial read contained a nul, assume nul-packet-terminating format stream */
	if((nul = strchr(buf,'\0')) != NULL && (nul - buf) < bufsz){
	  m->type = type_NUL;
	  if(nul[-2] == '/')
	    nul[-2] = ' '; /* assume it's .../>0 and make the stream open again */
	}
	/* XXX another big hack/experiment, for bypassing dumb proxies */
	if(*buf == 'P')
	    m->type = type_HTTP;
    }

    /* XXX more http hack to catch the end of the headers */
    if(m->type == type_HTTP){
	if((nul = strstr(buf,"\r\n\r\n")) == NULL)
	    return;
	nul += 4;
	bufsz = bufsz - (nul - buf);
	buf = nul;
	io_write(m,NULL,"HTTP/1.0 200 Ok\r\n"
				 "Server: jabber/xmlstream-hack-0.1\r\n"
				 "Expires: Fri, 10 Oct 1997 10:10:10 GMT\r\n"
				 "Pragma: no-cache\r\n"
				 "Cache-control: private\r\n"
				 "Connection: close\r\n\r\n",-1);
	m->type = type_NORMAL;
    }

    /* more nul-term hack to ditch the nul's whenever */
    if(m->type == type_NUL)
	while((nul = strchr(buf,'\0')) != NULL && (nul - buf) < bufsz){
	    memmove(nul,nul+1,strlen(nul+1));
	    bufsz--;
	}

    if( XML_Parse(m->parser, buf, bufsz, 0) == 0)
	if(m->cb != NULL){
	    (*(io_std_cb)m->cb)(m, IO_ERROR, m->cb_arg);
	    io_write(m, NULL, "<stream:error>Invalid XML</stream:error>", -1);
	    io_close(m);
	}
}










