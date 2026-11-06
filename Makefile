CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread

TARGETS = server client

all: $(TARGETS)

server: server.c chat.h
	$(CC) $(CFLAGS) -o $@ server.c

client: client.c chat.h
	$(CC) $(CFLAGS) -o $@ client.c

clean:
	rm -f $(TARGETS) *.o

run-server: server
	./server 8888

run-client: client
	./client 127.0.0.1 8888

test: all
	chmod +x test_chat.sh
	./test_chat.sh

.PHONY: all clean run-server run-client test
