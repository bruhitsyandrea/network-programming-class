CC = gcc
CFLAGS = -Wall -g

all: myweb

myweb: src/myweb.c
	$(CC) $(CFLAGS) src/myweb.c -o bin/myweb

clean:
	rm -f bin/myweb