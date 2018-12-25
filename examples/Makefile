CC      := /opt/m68k-amigaos/bin/m68k-amigaos-gcc
AS      := /opt/m68k-amigaos/bin/m68k-amigaos-as
CFLAGS  := -gstabs -Wall
LDFLAGS := -noixemul -L/opt/m68k-amigaos//m68k-amigaos/libnix/lib -L/opt/m68k-amigaos//m68k-amigaos/libnix/lib/libnix
LDLIBS  := -lamiga -lnix

.PHONY: all clean

all: stabs-test debug-test amihello loop

clean:
	rm -f *.o stabs-test debug-test amihello

amihello: cwcrt0.o amihello.o
	$(CC) $(LDFLAGS) -nostdlib -o $@ cwcrt0.o $@.o

loop: cwcrt0.o loop.o
	$(CC) $(LDFLAGS) -nostdlib -o $@ cwcrt0.o $@.o

debug-test.o: debug-test.s
	$(AS) -o $@ $^

debug-test: debug-test.o
	$(CC) $(LDFLAGS) -nostdlib -o $@.bad $^ && python3 -c 'import sys; data = open(sys.argv[1] + ".bad", "rb").read(); fh = open(sys.argv[1], "wb"); pos = data.find(b"trap\x0a"); fh.write(data[0:pos + 8]); fh.write(data[pos + 12:]); fh.close()' $@ && rm $@.bad
