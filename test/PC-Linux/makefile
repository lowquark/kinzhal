
SRCDIR=../../src/

.PHONY: all
all: ttyserial test

ttyserial: ttyserial.c $(SRCDIR)kazhal.c
	$(CC) -Wall -Wpedantic -g -o $@ $^ -I$(SRCDIR)

test: kazhal_test.c
	$(CC) -std=c89 -Wall -Wpedantic -g -o $@ $^ -I. -lcheck -I$(SRCDIR)

