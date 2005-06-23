# --------------------------------------------------------------------------
#
#  This program was developed by IDEALX (http://www.IDEALX.org), for
#  LibertySurf Télécom (http://www.libertysurftelecom.fr), based on
#  code developped by members of the Jabber Team in xdb/mysql.
#
#  Initial release in xdb_sql 1.0, (C) 2000 - 2001 LibertySurf
#  Télécom
#
#  Individual authors' names can be found in the AUTHORS file
#
#  Usage, modification and distribution rights reserved. See file
#  COPYING for details.
#
# --------------------------------------------------------------------------

# $Id: Makefile,v 1.4 2003/04/29 18:18:46 bobeal Exp $

#
# This Makefile is intended to be used in the Jabber environment
#

include ../platform-settings

VERSION = 1.3

# Debug version
CFLAGS += -DDEBUG -g -fPIC

# Select backend type (POSTGRESQL_BACKEND, ODBC_BACKEND,
# MYSQL_BACKEND, ORACLE_BACKEND)
BACKEND = POSTGRESQL_BACKEND

CFLAGS += -I. -I../jabberd -D$(BACKEND)

xdb_sql_OBJECTS= \
	xdb_sql.o \
	xdb_sql_auth0k.o \
	xdb_sql_auth.o \
	xdb_sql_offline.o \
	xdb_sql_register.o \
	xdb_sql_config.o \
	xdb_sql_querydef.o \
	xdb_sql_roster.o \
	xdb_sql_last.o \
	xdb_sql_backend.o \
	xdb_sql_vcard.o \
	xdb_sql_filter.o \
	xdb_sql_roomconfig.o \
	xdb_sql_roomoutcast.o \
	xdb_sql_generic.o 

ifeq ($(BACKEND),ORACLE_BACKEND)
  CFLAGS += -I$(ORACLE_HOME)/rdbms/demo/ -I$(ORACLE_HOME)/rdbms/public 
  LIBS += $(ORACLE_HOME)/lib/libclntsh.so
  xdb_sql_OBJECTS += xdb_sql_oracle.o
endif

ifeq ($(BACKEND),POSTGRESQL_BACKEND)
  CFLAGS += -I/usr/include
  LIBS += -L/usr/lib -lpq
  xdb_sql_OBJECTS += xdb_sql_pgsql.o
endif

ifeq ($(BACKEND),MYSQL_BACKEND)
  CFLAGS += -I/usr/include/mysql 
  LIBS += -L/usr/lib/mysql -lmysqlclient -lz
  xdb_sql_OBJECTS += xdb_sql_mysql.o
endif

ifeq ($(BACKEND),ODBC_BACKEND)
  xdb_sql_OBJECTS += xdb_sql_odbc.o
endif

all: xdb_sql.so

xdb_sql.so: $(xdb_sql_OBJECTS)
	$(CC) $(CFLAGS) $(MCFLAGS) -o xdb_sql.so $(xdb_sql_OBJECTS) $(LDFLAGS) $(LIBS) -static 

clean:
	rm -f $(xdb_sql_OBJECTS) xdb_sql.so
	rm -rf xdb_sql.$(VERSION)

dist: all
	mkdir xdb_sql.$(VERSION)
	cat FILES | cut -d":" -f 1 | xargs cp --target-directory=xdb_sql.$(VERSION)/
	tar cvzf xdb_sql.$(VERSION).tar.gz xdb_sql.$(VERSION)
