CC = gcc
CFLAGS = -I../include -g 
LDFLAGS = -L../ -leventloop -lpthread -Wl,-rpath,'$ORIGIN/..'

all: examples

examples: examples.c
	$(CC) $^ $(CFLAGS) $(LDFLAGS) -o $@

.PHONY: all clean

clean:
	rm -f examples