CC = gcc
CFLAGS = -lpthread -fPIC -Iinclude -g
LD_LIBRARY_PATH = $(PWD)/$(LIB_DIR)
EXAMPLES_CFLAGS = -lpthread -g -leventloop -L$(LD_LIBRARY_PATH) -Wl,-rpath,$(LD_LIBRARY_PATH)

BUILD_DIR ?= build
BIN_DIR   ?= $(BUILD_DIR)/bin
OBJ_DIR   ?= $(BUILD_DIR)/obj
LIB_DIR   ?= $(BUILD_DIR)/lib

all: $(LIB_DIR)/libeventloop.a $(LIB_DIR)/libeventloop.so $(BIN_DIR)/example

$(OBJ_DIR)/%.o: src/%.c include/%.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB_DIR)/libeventloop.a: $(OBJ_DIR)/event_loop.o
	@mkdir -p $(LIB_DIR)
	ar rcs $@ $^

$(LIB_DIR)/libeventloop.so: $(OBJ_DIR)/event_loop.o
	@mkdir -p $(LIB_DIR)
	${CC} -shared -o $@ $^

$(BIN_DIR)/example: examples/examples.c $(LIB_DIR)/libeventloop.so include/event_loop.h
	@mkdir -p $(BIN_DIR)
	$(CC) $< $(EXAMPLES_CFLAGS) -o $(BIN_DIR)/example

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
