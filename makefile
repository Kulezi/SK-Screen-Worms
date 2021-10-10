CC = g++
CFLAGS = -Wall -Wextra -std=c++17 -O2
LDFLAGS =

.PHONY: all clean

all: screen-worms-server screen-worms-client

screen-worms-server: screen-worms-server.o
	$(CC) $(LDFLAGS) -o $@ $^

screen-worms-server.o: screen-worms-server.cpp common.h
	$(CC) $(CFLAGS) -c $<

screen-worms-client: screen-worms-client.o
	$(CC) $(LDFLAGS) -o $@ $^

screen-worms-client.o: screen-worms-client.cpp screen-worms-client.h common.h
	$(CC) $(CFLAGS) -c $<


clean:
	rm -f screen-worms-server
	rm -f screen-worms-server.o
	rm -f screen-worms-client
	rm -f screen-worms-client.o