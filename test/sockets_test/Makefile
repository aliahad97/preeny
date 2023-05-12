CC = gcc
CFLAGS = -Wall

SERVER_SRC = server.c
SERVER_BIN = server

CLIENT_SRC = client.c
CLIENT_BIN = client

.PHONY: all clean

all: $(SERVER_BIN) $(CLIENT_BIN)

$(SERVER_BIN): $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $(SERVER_BIN) $(SERVER_SRC)

$(CLIENT_BIN): $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o $(CLIENT_BIN) $(CLIENT_SRC)

clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN)
