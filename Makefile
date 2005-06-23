
SUBDIRS=jabberd jsm xdb_file xdb_sql dialback dnsrv wpj_sysepoll

.PHONY: cosmetics ChangeLog

all: all-recursive

clean: clean-recursive

distclean: distclean-recursive
	rm -f platform-settings


dclean: distclean

all-recursive clean-recursive distclean-recursive:
	@set fnord $(MAKEFLAGS); amf=$$2; \
	dot_seen=no; \
	target=`echo $@ | sed s/-recursive//`; \
	list='$(SUBDIRS)'; for subdir in $$list; do \
	  echo "Making $$target in $$subdir"; \
	  (cd $$subdir && $(MAKE) $$target) \
	done; \

ChangeLog: 
	test -f .svn/entries && make cl-stamp || :
	
cl-stamp: .svn/entries
	TZ=UTC svn log -v --xml \
		| aux/svn2log.py -p '/wpjabber/(branches/[^/]+|trunk)' -x ChangeLog -u aux/users -F
	touch cl-stamp

cosmetics:
	./aux/cosmetics.sh

