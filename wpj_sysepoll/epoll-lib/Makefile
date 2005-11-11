#
#  epoll ( Efficent event polling implementation )
#  Copyright (C) 2001,...,2002	 Davide Libenzi
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  Davide Libenzi <davidel@xmailserver.org>
#
#

PREFIX=/usr
KERNELDIR=/usr/src/linux
VERMAJOR=0
VERMINOR=11
LIBVER=$(VERMAJOR).$(VERMINOR)

OUTDIR = lib
TARGET = $(OUTDIR)/libepoll.a
TARGET_SONAME = libepoll.so.$(VERMAJOR)
SO_TARGET = $(OUTDIR)/libepoll.so.$(LIBVER)

SRCDIR = ./src
INCLUDE = -I- -I./include -I$(KERNELDIR)/include

CC = gcc
LD = ld
AR = ar
MAKE = make

ifeq ($(EPOLLCFG), release)
CFLAGS = -O3 $(INCLUDE) -DUNIX
else
CFLAGS = -g $(INCLUDE) -DUNIX -D_DEBUG
endif

SOURCES = $(SRCDIR)/epoll.c

OBJECTS = $(OUTDIR)/epoll.o

MKDEP = ./mkdep -f .depend


$(OUTDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -o $(OUTDIR)/$*.o -c $(SRCDIR)/$*.c

all: .depend $(TARGET)

.depend: $(SOURCES)
	$(MKDEP) $(CFLAGS) $(SOURCES)

$(TARGET): $(OBJECTS)
	$(AR) -cr $(TARGET) $(OBJECTS)
	$(LD) -shared -soname $(TARGET_SONAME) -o $(SO_TARGET) $(OBJECTS)

epoll-example:
	$(MAKE) -C examples

install: $(TARGET)
	@cp -f include/epoll.h $(PREFIX)/include/sys
	@cp -f $(TARGET) $(SO_TARGET) $(PREFIX)/lib
	@cp -f man/*.2 $(PREFIX)/man/man2
	@cp -f man/*.4 $(PREFIX)/man/man4

distclean: clean
	@rm -f .depend

clean:
	@rm -f $(TARGET) $(SO_TARGET)
	@rm -f $(OBJECTS) *~

include .depend

