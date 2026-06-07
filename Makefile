CC      = gcc
CFLAGS  = -Wall -Wextra -g -pthread
COMMON  = common/logger.c common/config.c

.PHONY: all server client clean test

all: server client

server: $(COMMON) server/server.c
	$(CC) $(CFLAGS) $^ -o server/server_bin

client: $(COMMON) client/client.c
	$(CC) $(CFLAGS) $^ -o client/client_bin

clean:
	rm -f server/server_bin client/client_bin
	rm -f client/downloaded_update.pkg
	rm -f logs/*.log

test: all
	@echo "=== Running multi-client test ==="
	@bash test_multi.sh
