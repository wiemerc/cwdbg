.PHONY: all clean server examples

all: server examples

clean:
	$(MAKE) --directory=server clean
	$(MAKE) --directory=examples clean

server:
	docker run -d --name cwdebug-build-server --rm cwdebug-build-server
	docker cp $@ cwdebug-build-server:/tmp/build/
	docker exec -w /tmp/build cwdebug-build-server make
	docker cp cwdebug-build-server:/tmp/build/. $@
	docker kill cwdebug-build-server

examples:
	$(MAKE) --directory=$@

stats:
	cloc --not-match-d='\.venv|misc' --not-match-f='m68k.+' --fullpath --exclude-ext=d,json --force-lang=C,h .
