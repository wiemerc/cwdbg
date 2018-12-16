CC      := /opt/m68k-amigaos/bin/m68k-amigaos-gcc
AS      := /opt/m68k-amigaos/bin/m68k-amigaos-as
CFLAGS  := -Wall
LDFLAGS := -s -noixemul -L/opt/m68k-amigaos//m68k-amigaos/libnix/lib -L/opt/m68k-amigaos//m68k-amigaos/libnix/lib/libnix
LDLIBS  := -lnix -lamiga

.PHONY: all clean examples

all: cwdebug examples

clean:
	rm -f *.o m68k.h m68kdasm.c cwdebug
	$(MAKE) --directory=examples clean

%.h: %.h.patch
	wget -q https://raw.githubusercontent.com/kstenerud/Musashi/master/$@
	patch -p0 < $@.patch

%.c: %.c.patch
	wget -q https://raw.githubusercontent.com/kstenerud/Musashi/master/$@
	patch -p0 < $@.patch

main.o: main.c util.h m68k.h

util.o: util.c util.h

glue.o: glue.s
	$(AS) -o $@ $^

m68kdasm.o: m68kdasm.c m68k.h

cwdebug: main.o util.o glue.o m68kdasm.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

examples:
	$(MAKE) --directory=$@
