
.PHONY: all
all: ttyserial test example

ttyserial: ttyserial.c kazhal.c
	$(CC) -Wall -Wpedantic -g -o $@ $^ -I.

test: kazhal_test.c
	$(CC) -std=c89 -Wall -Wpedantic -g -o $@ $^ -I. -lcheck

example: example.c kazhal.c
	$(CC) -std=c89 -Wall -Wpedantic -g -o $@ $^ -I.

