CC      := /opt/m68k-amigaos/bin/m68k-amigaos-gcc
AS      := /opt/m68k-amigaos/bin/m68k-amigaos-as
CFLAGS  := -Wall
LDFLAGS := -s -noixemul -L/opt/m68k-amigaos//m68k-amigaos/libnix/lib -L/opt/m68k-amigaos//m68k-amigaos/libnix/lib/libnix
LDLIBS  := -lamiga -lnix

.PHONY: all clean examples

all: cwdebug examples

clean:
	rm -f *.o cwdebug

main.o: main.c util.h

glue.o: glue.s
	$(AS) -o $@ $^

cwdebug: main.o glue.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

examples:
	$(MAKE) --directory=$@
