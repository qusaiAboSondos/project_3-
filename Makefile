CC      = gcc
CFLAGS  = -Wall -Wextra -g -pthread
GL_LIBS = -lGL -lGLU -lglut -lm
COMMON  = common/logger.c common/config.c common/shared_state.c

.PHONY: all server client clean test

all: server client

server: $(COMMON) server/server.c server/visualizer.c
	$(CC) $(CFLAGS) $^ -o server/server_bin $(GL_LIBS)

client: $(COMMON) client/client.c
	$(CC) $(CFLAGS) $^ -o client/client_bin

clean:
	rm -f server/server_bin client/client_bin
	rm -f client/downloaded_update.pkg
	rm -f logs/*.log

test: all
	@echo "=== Running multi-client test ==="
	@bash test_multi.sh
