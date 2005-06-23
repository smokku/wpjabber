
include ../platform-settings

CFLAGS=$(WPJ_CFLAGS) 
#-I../jabberd/lib -DHAVE_SSL -I epoll-lib/include
LIBS=$(WPJ_LIBS)
# -lssl -lcrypto -Lepoll-lib/lib -lepoll

JABD=../jabberd

jabberd_XODEOBJECTS = \
	$(JABD)/lib/expat.o \
	$(JABD)/lib/genhash.o \
	$(JABD)/lib/jid.o \
	$(JABD)/lib/jpacket.o \
	$(JABD)/lib/jutil.o \
	$(JABD)/lib/karma.o \
	$(JABD)/lib/pool.o \
	$(JABD)/lib/pproxy.o \
	$(JABD)/lib/rate.o \
	$(JABD)/lib/sha.o \
	$(JABD)/lib/snprintf.o \
	$(JABD)/lib/socket.o \
	$(JABD)/lib/str.o \
	$(JABD)/lib/xmlnode.o \
	$(JABD)/lib/xstream.o \
	$(JABD)/lib/xhash.o 


wpj_OBJECTS= \
	connection.o \
	debug.o \
	masters.o \
	io.o \
	ioxml.o \
	ioraw.o \
	iossl.o \
	wpj.o \
	log.o  \
	rate.o 

all_OBJECTS=$(jabberd_XODEOBJECTS) $(wpj_OBJECTS)

all: $(WPJ_BUILD)

$(WPJ_BIN): $(all_OBJECTS)
	$(CC) -g -o $(WPJ_BIN) $(all_OBJECTS) $(LIBS)

epoll_lib:
	(cd epoll-lib && make)

$(wpj_OBJECTS): wpj.h

$(all_OBJECTS): ../platform-settings

clean:
	rm -f $(wpj_OBJECTS) $(jabberd_XODEOBJECTS) *~ $(JABD)/lib/*~
	(cd epoll-lib && make clean)

distclean: clean
	rm -f $(WPJ_BIN)
	(cd epoll-lib && make distclean)

