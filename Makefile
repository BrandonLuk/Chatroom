CC = gcc
CFLAGS = -Wall -std=c99

all: client server

client: client.c terminal.c
	$(CC) $(CFLAGS) client.c terminal.c -o client.exe

server: server.c terminal.c
	$(CC) $(CFLAGS) server.c terminal.c -o server.exe

clean: 
	rm *.exe