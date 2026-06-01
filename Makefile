CC      = gcc
CFLAGS  = -Wall -Wextra -g

all: server client

server: server.c user_mng.c protocol.h user.h user_mng.h
	$(CC) $(CFLAGS) -o server server.c user_mng.c -lpthread

client: client.c protocol.h
	$(CC) $(CFLAGS) -o client client.c

clean:
	rm -f server client

.PHONY: all clean
