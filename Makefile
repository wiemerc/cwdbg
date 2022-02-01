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
