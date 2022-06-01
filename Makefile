.PHONY: all clean server examples

all: server examples

clean:
	$(MAKE) --directory=server clean
	$(MAKE) --directory=examples clean

server:
	docker run -d --name cwdebug-build-server-container --rm cwdebug-build-server && \
	docker cp $@ cwdebug-build-server-container:/tmp/build/ && \
	(docker exec -w /tmp/build cwdebug-build-server-container make || (docker kill cwdebug-build-server-container; exit 1)) && \
	docker cp cwdebug-build-server-container:/tmp/build/. $@ && \
	docker kill cwdebug-build-server-container

examples:
	$(MAKE) --directory=$@

stats:
	cloc --not-match-d='\.venv|misc' --not-match-f='m68k.+' --fullpath --exclude-ext=d,json --force-lang=C,h .
