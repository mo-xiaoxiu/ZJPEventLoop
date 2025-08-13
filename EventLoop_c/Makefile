CC = gcc
CFLAGS = -lpthread -fPIC -Iinclude -g
LD_LIBRARY_PATH = $(PWD)
EXAMPLES_CFLAGS = -lpthread -g -leventloop -L$(LD_LIBRARY_PATH) -Wl,-rpath,$(LD_LIBRARY_PATH)

all: libeventloop.a libeventloop.so example

src/event_loop.o: src/event_loop.c include/event_loop.h
	${CC} ${CFLAGS} -c $< -o $@

libeventloop.a: src/event_loop.o
	ar rcs $@ $^

libeventloop.so: src/event_loop.o
	${CC} -shared -o $@ $^

example: examples/examples.c libeventloop.so include/event_loop.h
	$(CC) $< $(EXAMPLES_CFLAGS) -o example

clean:
	rm -f src/*.o *.a *.so example

.PHONY: all clean
