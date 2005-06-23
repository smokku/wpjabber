
include ../platform-settings

DEFINES=-DHOME="\"$(PREFIX)\""

CFLAGS:=$(COMMON_CFLAGS) $(DEFINES)

jabberd_HEADERS=jabberd.h

jabberd_OBJECTS = \
	config.o \
	mio.o \
	mio_raw.o \
	mio_xml.o \
	mio_ssl.o \
	deliver.o \
	heartbeat.o \
	jabberd.o \
	load.o \
	xdb.o \
	mtq.o \
	static.o \
	log.o

jabberd_EXTRALIB = \
	extralib/wphash.o \
	extralib/debug.o \
	extralib/fast_mtq.o 

jabberd_XODEOBJECTS = \
	lib/expat.o \
	lib/genhash.o \
	lib/jid.o \
	lib/jpacket.o \
	lib/jutil.o \
	lib/karma.o \
	lib/pool.o \
	lib/pproxy.o \
	lib/rate.o \
	lib/sha.o \
	lib/snprintf.o \
	lib/socket.o \
	lib/str.o \
	lib/xmlnode.o \
	lib/xstream.o \
	lib/xhash.o

jabberd_BASEOBJECTS = \
	base/base_accept.o \
	base/base_connect.o \
	base/base_file.o \
	base/base_format.o \
	base/base_stderr.o \
	base/base_to.o

jabberd_EXOBJECTS = $(jabberd_EXTRALIB) $(jabberd_XODEOBJECTS) $(jabberd_BASEOBJECTS) 
jabberd_COMMONOBJS = $(jabberd_OBJECTS) $(jabberd_EXOBJECTS)

CFLAGS=$(COMMON_CFLAGS) $(DEFINES)
LIBS=$(COMMON_LIBS) 


all: $(OUT_FILE)

$(OUT_FILE): $(jabberd_COMMONOBJS) 
	gcc -o $(OUT_FILE) $(jabberd_COMMONOBJS) -Wl,--export-dynamic $(LIBS)

clean:
	rm -f *~ *.o
	rm -f lib/*~ lib/*.o
	rm -f base/*~ base/*.o
	rm -f extralib/*~ extralib/*.o

distclean: clean
	rm -f $(OUT_FILE)

