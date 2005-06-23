
include ../platform-settings

jsm_MODULES=deliver.o \
	jsm.o \
	modules.o \
	offline.o \
	server.o \
	authreg.o \
	postout.o \
	sessions.o \
	users.o \
	util.o \
	util_roster.o \
	modules/mod_admin.o \
	modules/mod_agents.o \
	modules/mod_announce.o \
	modules/mod_auth_plain.o \
	modules/mod_auth_digest.o \
	modules/mod_browse.o \
	modules/mod_disco.o \
	modules/mod_echo.o \
	modules/mod_event.o \
	modules/mod_privacy.o \
	modules/mod_last.o \
	modules/mod_log.o \
	modules/mod_offline.o \
	modules/mod_offline_count.o \
	modules/mod_presence.o \
	modules/mod_packet_stats.o \
	modules/mod_roster.o \
	modules/mod_roster_auto.o \
	modules/mod_register.o \
	modules/mod_stats.o \
	modules/mod_status_offline.o \
	modules/mod_time.o \
	modules/mod_version.o \
	modules/mod_vcard.o \
	modules/mod_xml.o

jsm_LIB=jsm.so

CFLAGS=$(COMMON_CFLAGS)
LIBS=$(COMMONLIBS)

all: dynamic

dynamic: $(jsm_LIB)

$(jsm_LIB): $(jsm_MODULES)
	$(CC) -shared -o $(jsm_LIB) $(jsm_MODULES) $(CFLAGS) $(LIBS) 

$(jsm_MODULES): jsm.h

clean:
	rm -f *~ $(jsm_MODULES) modules/*~

distclean: clean
	rm -f $(jsm_LIB)

