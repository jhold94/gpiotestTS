#
# Makefile:
#	The ts7680ctl command:
#
#	Copyright (c) 2017 Joshua Holder - Custom Controls Unlimited Inc.
#################################################################################
# This file is part of ts7680ctl:
#	A pin control library for the TS-7680
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

DESTDIR?=/usr
PREFIX?=/local

ifneq ($V,1)
Q ?= @
endif

#DEBUG	= -g -O0
DEBUG	= -O2
CC	= gcc
INCLUDE	= -I$(DESTDIR)$(PREFIX)/include
CFLAGS	= $(DEBUG) -Wall -Wextra $(INCLUDE) -Winline -pipe

LDFLAGS	= -L$(DESTDIR)$(PREFIX)/lib
LIBS    = -lpthread -lrt -lm -lcrypt

# -lts7680ctl -lts7680ctlDev 

# May not need to  alter anything below this line
###############################################################################

SRC	=	ts7680ctl.c

OBJ	=	$(SRC:.c=.o)

all:		ts7680ctl

version.h:	../VERSION
	$Q echo Need to run newVersion above.

ts7680ctl:	$(OBJ)
	$Q echo [Link]
	$Q $(CC) -o $@ $(OBJ) $(LDFLAGS) $(LIBS)

.c.o:
	$Q echo [Compile] $<
	$Q $(CC) -c $(CFLAGS) $< -o $@

.PHONY:	clean
clean:
	$Q echo "[Clean]"
	$Q rm -f $(OBJ) ts7680ctl *~ core tags *.bak

.PHONY:	tags
tags:	$(SRC)
	$Q echo [ctags]
	$Q ctags $(SRC)

.PHONY:	install
install: ts7680ctl
	$Q echo "[Install]"
	$Q cp ts7680ctl		$(DESTDIR)$(PREFIX)/bin
ifneq ($(TS7680CTL_SUID),0)
	$Q chown root.root	$(DESTDIR)$(PREFIX)/bin/ts7680ctl
	$Q chmod 4755		$(DESTDIR)$(PREFIX)/bin/ts7680ctl
endif
	$Q mkdir -p		$(DESTDIR)$(PREFIX)/man/man1
	$Q cp ts7680ctl.1		$(DESTDIR)$(PREFIX)/man/man1

.PHONY:	install-deb
install-deb:	ts7680ctl
	$Q echo "[Install: deb]"
	$Q install -m 0755 -d							~/ts7680ctl/debian-template/ts7680ctl/usr/bin
	$Q install -m 0755 ts7680ctl							~/ts7680ctl/debian-template/ts7680ctl/usr/bin
	$Q install -m 0755 -d							~/ts7680ctl/debian-template/ts7680ctl/man/man1
	$Q install -m 0644 ts7680ctl.1						~/ts7680ctl/debian-template/ts7680ctl/man/man1

.PHONY:	uninstall
uninstall:
	$Q echo "[UnInstall]"
	$Q rm -f $(DESTDIR)$(PREFIX)/bin/ts7680ctl
	$Q rm -f $(DESTDIR)$(PREFIX)/man/man1/ts7680ctl.1

.PHONY:	depend
depend:
	makedepend -Y $(SRC)

# DO NOT DELETE

ts7680ctl.o: ../version.h
