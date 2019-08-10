CC=gcc
CFLAGS=-pthread -Wall -Wextra

cdht:	cdht.c
	$(CC) $(CFLAGS) -o cdht cdht.c