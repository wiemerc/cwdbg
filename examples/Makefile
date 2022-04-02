CC      := /opt/m68k-amigaos/bin/m68k-amigaos-gcc
AS      := /opt/m68k-amigaos/bin/m68k-amigaos-as
CFLAGS  := -g -Wall
LDFLAGS := -noixemul -L/opt/m68k-amigaos//m68k-amigaos/libnix/lib -L/opt/m68k-amigaos//m68k-amigaos/libnix/lib/libnix
LDLIBS  := -lamiga -lnix

.PHONY: all clean

all: stabs-test debug-test amihello loop crash

clean:
	rm -f *.o stabs-test debug-test amihello loop

amihello: cwcrt0.o amihello.o
	$(CC) $(LDFLAGS) -nostdlib -o $@ cwcrt0.o $@.o

%.o: %.s
	$(AS) -o $@ $^

debug-test.o: debug-test.s
	$(AS) -o $@ $^

debug-test: debug-test.o
	$(CC) $(LDFLAGS) -nostdlib -o $@.bad $^ && python3 -c 'import sys; data = open(sys.argv[1] + ".bad", "rb").read(); fh = open(sys.argv[1], "wb"); pos = data.find(b"trap\x0a"); fh.write(data[0:pos + 8]); fh.write(data[pos + 12:]); fh.close()' $@ && rm $@.bad

loop: loop.o
	$(CC) $(LDFLAGS) -nostdlib -o $@ $^

crash: crash.o ../server/dump-core.o ../server/catch-exc.o
	$(CC) $(LDFLAGS) -o $@  $@.o ../server/dump-core.o ../server/catch-exc.o
