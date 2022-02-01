CC       := /opt/m68k-amigaos/bin/m68k-amigaos-gcc
AS       := /opt/m68k-amigaos/bin/m68k-amigaos-as
CFLAGS   := -Wall -MMD
LDFLAGS  := -s -noixemul -L/opt/m68k-amigaos//m68k-amigaos/libnix/lib -L/opt/m68k-amigaos//m68k-amigaos/libnix/lib/libnix
LDLIBS   := -lnix -lamiga -ldebug
SRCFILES := cli.c debugger.c m68kdasm.c main.c serio.c util.c

.PHONY: all clean server examples

all: server examples

clean:
	$(MAKE) --directory=server clean
	$(MAKE) --directory=examples clean

server:
	$(MAKE) --directory=$@

examples:
	$(MAKE) --directory=$@

stats:
	cloc --not-match-d='\.venv|misc' --not-match-f='m68k.+' --fullpath --exclude-ext=d,json --force-lang=C,h .
