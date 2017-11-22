#
# Makefile:
#	ts7680ctl - Wiring Compatable library for the TS-7680
#
#	Copyright (c) 2017 Joshua Holder - Custom Controls Unlimited Inc.
#################################################################################
# This file is part of ts7680ctl:
#
#    ts7680ctl is free software: you can redistribute it and/or modify
#    it under the terms of the GNU Lesser General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    ts7680ctl is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU Lesser General Public License for more details.
#
#    You should have received a copy of the GNU Lesser General Public License
#    along with ts7680ctl.  If not, see <http://www.gnu.org/licenses/>.
#################################################################################

VERSION=$(shell cat ../VERSION)
DESTDIR?=/usr
PREFIX?=/local

LDCONFIG?=ldconfig

ifneq ($V,1)
Q ?= @
endif

STATIC=libts7680ctl.a
DYNAMIC=libts7680ctl.so.$(VERSION)

#DEBUG	= -g -O0
DEBUG	= -O2
CC	= gcc
INCLUDE	= -I.
DEFS	= -D_GNU_SOURCE
CFLAGS	= $(DEBUG) $(DEFS) -Wformat=2 -Wall -Wextra -Winline $(INCLUDE) -pipe -fPIC

LIBS    = -lm -lpthread -lrt -lcrypt

###############################################################################

SRC	=	ts7680ctl.c

HEADERS =	$(shell ls *.h)

OBJ	=	$(SRC:.c=.o)

all:		$(DYNAMIC)

static:		$(STATIC)

$(STATIC):	$(OBJ)
	$Q echo "[Link (Static)]"
	$Q ar rcs $(STATIC) $(OBJ)
	$Q ranlib $(STATIC)
#	@size   $(STATIC)

$(DYNAMIC):	$(OBJ)
	$Q echo "[Link (Dynamic)]"
	$Q $(CC) -shared -Wl,-soname,libts7680ctl.so$(TS7680CTL_SONAME_SUFFIX) -o libts7680ctl.so.$(VERSION) $(LIBS) $(OBJ)

.c.o:
	$Q echo [Compile] $<
	$Q $(CC) -c $(CFLAGS) $< -o $@


.PHONY:	clean
clean:
	$Q echo "[Clean]"
	$Q rm -f $(OBJ) $(OBJ_I2C) *~ core tags Makefile.bak libts7680ctl.*

.PHONY:	tags
tags:	$(SRC)
	$Q echo [ctags]
	$Q ctags $(SRC)


.PHONY:	install
install:	$(DYNAMIC)
	$Q echo "[Install Headers]"
	$Q install -m 0755 -d						$(DESTDIR)$(PREFIX)/include
	$Q install -m 0644 $(HEADERS)					$(DESTDIR)$(PREFIX)/include
	$Q echo "[Install Dynamic Lib]"
	$Q install -m 0755 -d						$(DESTDIR)$(PREFIX)/lib
	$Q install -m 0755 libts7680ctl.so.$(VERSION)			$(DESTDIR)$(PREFIX)/lib/libts7680ctl.so.$(VERSION)
	$Q ln -sf $(DESTDIR)$(PREFIX)/lib/libts7680ctl.so.$(VERSION)	$(DESTDIR)/lib/libts7680ctl.so
	$Q $(LDCONFIG)

.PHONY:	install-static
install-static:	$(STATIC)
	$Q echo "[Install Headers]"
	$Q install -m 0755 -d						$(DESTDIR)$(PREFIX)/include
	$Q install -m 0644 $(HEADERS)					$(DESTDIR)$(PREFIX)/include
	$Q echo "[Install Static Lib]"
	$Q install -m 0755 -d						$(DESTDIR)$(PREFIX)/lib
	$Q install -m 0755 libts7680ctl.a					$(DESTDIR)$(PREFIX)/lib

.PHONY:	install-deb
install-deb:	$(DYNAMIC)
	$Q echo "[Install Headers: deb]"
	$Q install -m 0755 -d							~/ts7680ctl/debian-template/ts7680ctl/usr/include
	$Q install -m 0644 $(HEADERS)						~/ts7680ctl/debian-template/ts7680ctl/usr/include
	$Q echo "[Install Dynamic Lib: deb]"
	install -m 0755 -d							~/ts7680ctl/debian-template/ts7680ctl/usr/lib
	install -m 0755 libwiringPi.so.$(VERSION)				~/ts7680ctl/debian-template/ts7680ctl/usr/lib/libts7680ctl.so.$(VERSION)
	ln -sf ~/ts7680ctl/debian-template/ts7680ctl/usr/lib/libts7680ctl.so.$(VERSION)	~/ts7680ctl/debian-template/ts7680ctl/usr/lib/libts7680ctl.so

.PHONY:	uninstall
uninstall:
	$Q echo "[UnInstall]"
	$Q cd $(DESTDIR)$(PREFIX)/include/ && rm -f $(HEADERS)
	$Q cd $(DESTDIR)$(PREFIX)/lib/     && rm -f libts7680ctl.*
	$Q $(LDCONFIG)


.PHONY:	depend
depend:
	makedepend -Y $(SRC) $(SRC_I2C)

# DO NOT DELETE

ts7680ctl.o: ../version.h
