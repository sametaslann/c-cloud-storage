CC = gcc
CFLAGS = -D_DEFAULT_SOURCE -Wall  -pedantic -lrt -pthread -std=c11 

SERVER_SRC = server.c 
CLIENT_SRC = client.c 

SERVER_OBJ = $(SERVER_SRC:.c=.o)
CLIENT_OBJ = $(CLIENT_SRC:.c=.o)

all: server client

server: $(SERVER_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

client: $(CLIENT_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f server client $(SERVER_OBJ) $(CLIENT_OBJ)
