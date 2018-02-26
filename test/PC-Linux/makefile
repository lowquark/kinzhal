
SRCDIR=../../src/

.PHONY: all
all: ttyserial test

ttyserial: ttyserial.c $(SRCDIR)kinzhal.c
	$(CC) -Wall -Wpedantic -g -o $@ $^ -I$(SRCDIR)

test: kinzhal_test.c
	$(CC) -std=c89 -Wall -Wpedantic -g -o $@ $^ -I. -lcheck -I$(SRCDIR)

