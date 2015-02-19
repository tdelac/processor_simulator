CFLAGS=-Wall -Wextra -pedantic -std=c99 -O2 -lm
CC=gcc
SRCS=simulator.c
OBJS=simulator.o
LDFLAGS=
LIBS=

all: simulator

$(SRCS):
	$(CC) $(CFLAGS) -c $*.c

simulator: $(OBJS)
	$(CC) $(LDFLAGS) $(LIBS) -o simulator $(OBJS) -lm

clean:
	rm -rf simulator.o simulator
