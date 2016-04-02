CC = gcc -g
CFLAGS = -Wall -Werror 

all: jimmysh

jimmysh: jimmysh.c
	$(CC) $(CFLAGS) -o jimmysh jimmysh.c

clean:
	rm -f *~ *.o jimmysh
