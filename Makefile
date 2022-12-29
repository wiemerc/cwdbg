.PHONY: all clean server examples

all: server examples

clean:
	$(MAKE) --directory=server clean
	$(MAKE) --directory=examples clean

server:
	docker run -d --name cwdbg-build-server-container --rm cwdbg-build-server && \
	docker cp $@ cwdbg-build-server-container:/tmp/build/ && \
	(docker exec -w /tmp/build cwdbg-build-server-container make || (docker kill cwdbg-build-server-container; exit 1)) && \
	docker cp cwdbg-build-server-container:/tmp/build/. $@ && \
	docker kill cwdbg-build-server-container

examples:
	$(MAKE) --directory=$@

stats:
	cloc --not-match-d='\.venv|misc' --not-match-f='m68k.+' --fullpath --exclude-ext=d,json,csv,svg --force-lang=C,h .
