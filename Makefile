#
# $Id: Makefile,v 1.3 2000/11/18 00:30:52 tgkamp Exp $
#
# picoTK frame buffer emulator Makefile
#
# (c) Copyright 2000 by Thomas Gallenkamp <tgkamp@users.sourceforge.net>
#

XLIB=/usr/X11R6/lib

CC=gcc
CFLAGS=-Wall -g
LDFLAGS=-L/usr/X11R6/lib
LDLIBS=-lX11

all: fbe

fbe: fbe.o 

clean: 
	rm -f fbe  *.o fbe_buffer *~
