# fussC - portable Makefile (works with BSD make and GNU make).
# Knobs (override on the command line): CC, OPT, DBG, PREFIX, CFLAGS, LDFLAGS.
#   make                build fussy
#   make test           build and run the test suite
#   make OPT="-O0 -g" DBG="-fsanitize=address,undefined"   sanitizer build
#   make clean

CC      ?= cc
PREFIX  ?= /usr/local
BINDIR   = $(PREFIX)/bin
OPT     ?= -O2
DBG     ?=

CSTD     = -std=c11 -D_POSIX_C_SOURCE=200809L
WARN     = -Wall -Wextra -Werror
CFLAGS   = $(CSTD) $(WARN) $(OPT) $(DBG) -Iinclude -Itest
LDLIBS   = -lpthread

BIN      = fussy

# Object lists are explicit (no wildcard) so BSD make and GNU make agree.
LIBOBJS  = src/term.o src/util.o src/tree.o src/flatten.o
OBJS     = src/main.o $(LIBOBJS)

TESTBIN  = test/run
TESTOBJS = test/test_main.o test/test_util.o test/test_status.o \
           test/test_tree.o test/test_flatten.o

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

test: $(TESTBIN)
	./$(TESTBIN)

$(TESTBIN): $(TESTOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(TESTOBJS) $(LIBOBJS) $(LDLIBS)

clean:
	rm -f $(BIN) $(OBJS) $(TESTBIN) $(TESTOBJS)

install: $(BIN)
	mkdir -p $(DESTDIR)$(BINDIR)
	cp $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)

.PHONY: all test clean install uninstall
