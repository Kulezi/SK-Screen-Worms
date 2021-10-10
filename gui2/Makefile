SRC=cmd.c err.c net.c read_line.c

CC = gcc

CFLAGS=-std=c99 -Wall -Wunused -DDEBUG

gui2: gui2.c $(SRC) gui.h 
	$(CC) $(CFLAGS) gui2.c $(SRC) -o gui2 `pkg-config gtk+-2.0 --cflags --libs`

clean: 
	rm -f *.o gui2
