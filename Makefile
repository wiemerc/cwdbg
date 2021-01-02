CC      := /opt/m68k-amigaos/bin/m68k-amigaos-gcc
AS      := /opt/m68k-amigaos/bin/m68k-amigaos-gcc
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

# TODO: use current version of Musashi
%.h: %.h.patch
	rm -f $@ && wget -q https://raw.githubusercontent.com/kstenerud/Musashi/3ae92a71669a3689fc01a25191e46afa68dfdcce/$@
	patch -p0 < $@.patch

%.c: %.c.patch
	rm -f $@ && wget -q https://raw.githubusercontent.com/kstenerud/Musashi/3ae92a71669a3689fc01a25191e46afa68dfdcce/$@
	patch -p0 < $@.patch

# TODO: Can we generate the dependencies?

cli.o: cli.c cli.h util.h debugger.h m68k.h

debugger.o: debugger.c debugger.h util.h

m68kdasm.o: m68kdasm.c m68k.h

main.o: main.c util.h serio.h

serio.o: serio.c serio.h util.h debugger.h

util.o: util.c util.h

exc-handler.o: exc-handler.s
	$(AS) -c -o $@ $^

cwdebug: cli.o debugger.o m68kdasm.o main.o serio.o util.o exc-handler.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

examples:
	$(MAKE) --directory=$@
