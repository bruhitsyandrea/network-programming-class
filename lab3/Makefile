CC = gcc
CFLAGS = -Wall -Wextra -Wunused -g

all: myclient myserver

myclient: src/myclient.c
	$(CC) $(CFLAGS) src/myclient.c -o bin/myclient

myserver: src/myserver.c
	$(CC) $(CFLAGS) src/myserver.c -o bin/myserver

clean:
	rm -f bin/myclient bin/myserver
