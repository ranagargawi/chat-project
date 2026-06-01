CC     = gcc
CFLAGS = -Wall -Wextra -g -D_GNU_SOURCE

all: server client chat_window chat_sender chat_receiver

server: server.c user_mng.c session_mng.c group_mng.c \
        protocol.h user.h user_mng.h session_mng.h group.h group_mng.h
	$(CC) $(CFLAGS) -o server server.c user_mng.c session_mng.c group_mng.c \
	    -lpthread

client: client.c protocol.h mq_msg.h
	$(CC) $(CFLAGS) -o client client.c

chat_sender: chat_sender.c protocol.h mq_msg.h
	$(CC) $(CFLAGS) -o chat_sender chat_sender.c

chat_receiver: chat_receiver.c protocol.h mq_msg.h
	$(CC) $(CFLAGS) -o chat_receiver chat_receiver.c

chat_window: chat_window.c ui.c protocol.h mq_msg.h ui.h
	$(CC) $(CFLAGS) -o chat_window chat_window.c ui.c

clean:
	rm -f server client chat_sender chat_receiver chat_window

.PHONY: all clean