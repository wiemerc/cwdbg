CC      := /opt/m68k-amigaos/bin/m68k-amigaos-gcc
AS      := /opt/m68k-amigaos/bin/m68k-amigaos-as
CFLAGS  := -Wall
LDFLAGS := -s -noixemul -L/opt/m68k-amigaos//m68k-amigaos/libnix/lib -L/opt/m68k-amigaos//m68k-amigaos/libnix/lib/libnix
LDLIBS  := -lnix -lamiga -ldebug

.PHONY: all clean examples

all: cwdebug examples

clean:
	rm -f *.o m68k.h m68kdasm.c cwdebug
	$(MAKE) --directory=examples clean

history:
	git log --format="format:%h %ci %s"

%.h: %.h.patch
	rm -f $@ && wget -q https://raw.githubusercontent.com/kstenerud/Musashi/master/$@
	patch -p0 < $@.patch

%.c: %.c.patch
	rm -f $@ && wget -q https://raw.githubusercontent.com/kstenerud/Musashi/master/$@
	patch -p0 < $@.patch

debugger.o: debugger.c debugger.h util.h

m68kdasm.o: m68kdasm.c m68k.h

main.o: main.c util.h m68k.h

util.o: util.c util.h

glue.o: glue.s
	$(AS) -o $@ $^

cwdebug: debugger.o m68kdasm.o main.o util.o glue.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

examples:
	$(MAKE) --directory=$@
