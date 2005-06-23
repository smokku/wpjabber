
include ../platform-settings

jabberd_HEADERS=

module_HEADERS=dialback.h

module_MODULES=dialback.o \
	dialback_in.o \
	dialback_out.o

module_LIB=dialback.so

LIBS=
CFLAGS=$(COMMON_CFLAGS) $(module_CFLAGS)

all: dynamic

dynamic: $(module_LIB)

$(module_LIB): $(module_MODULES)
	$(CC) -shared -o $(module_LIB) $(module_MODULES) $(LIBS) 

clean:
	rm -f *~ $(module_MODULES) 

distclean: clean
	rm -f $(module_LIB)

