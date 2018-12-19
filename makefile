CC = gcc
CFLAGS = -Wall  -pthread -O0

all: client server

client: parallel_calc.o client.c
	$(CC) $(CFLAGS) -o $@ $^
server: parallel_calc.o server.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -rf *.o integral

.PHONY: all clean
