CC      := /opt/m68k-amigaos/bin/m68k-amigaos-gcc
CFLAGS  := -Wall
LDFLAGS := -s -noixemul -L/opt/m68k-amigaos//m68k-amigaos/libnix/lib -L/opt/m68k-amigaos//m68k-amigaos/libnix/lib/libnix
LDLIBS  := -lamiga -lnix

.PHONY: all clean

all: cwdebug

clean:
	rm -f *.o cwdebug

main.o: main.c util.h

cwdebug: main.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
