CC = gcc

CFLAGS = -Wall -pthread -O2 -I/opt/homebrew/opt/openssl@3/include
LDFLAGS = -L/opt/homebrew/opt/openssl@3/lib -lssl -lcrypto

OBJ = bin/myproxy.o bin/connection.o bin/filtering.o bin/logging.o

all: bin/myproxy

bin/myproxy: $(OBJ) | bin
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

bin/%.o: src/%.c | bin
	$(CC) $(CFLAGS) -c $< -o $@

bin:
	mkdir -p bin

clean:
	rm -rf bin/myproxy bin/*.o
