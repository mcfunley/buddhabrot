CC=gcc 
CFLAGS=-g -O3 -Wall
sources=buddhabrot.c
libs=/usr/local/lib/libtiff.dylib

all: 
	$(CC) $(CFLAGS) $(sources) $(libs) -o buddhabrot
