
include ../platform-settings

jabberd_HEADERS=

module_HEADERS=srv_resolv.h

module_MODULES=wpdnsrv.o \
	srv_resolv.o 

module_LIB=dnsrv.so

LIBS= -lresolv
CFLAGS=$(COMMON_CFLAGS) $(module_CFLAGS)

all: dynamic

dynamic: $(module_LIB)

$(module_LIB): $(module_MODULES)
	$(CC) -shared -o $(module_LIB) $(module_MODULES) $(LIBS) 

clean:
	rm -f *~ $(module_MODULES) 

distclean: clean
	rm -f $(module_LIB)

