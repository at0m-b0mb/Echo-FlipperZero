# Host tests for Echo's name classifier and exposure score.
#
# Both modules are deliberately free of Flipper headers, so they compile and
# run on a laptop under the same warning settings the firmware build uses.
#
#     make -C test

CC ?= cc
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror -Wshadow -Wconversion

BIN := host_echo_test
SRC := host_echo_test.c ../helpers/probe_intel.c ../helpers/exposure.c
HDR := ../helpers/probe_intel.h ../helpers/exposure.h

.PHONY: all clean
all: $(BIN)
	./$(BIN)

$(BIN): $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC)

clean:
	rm -rf $(BIN) $(BIN).dSYM
