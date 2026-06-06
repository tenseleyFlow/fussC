# fussC - portable Makefile (works with BSD make and GNU make).
# Knobs (override on the command line): CC, OPT, DBG, PREFIX, CFLAGS, LDFLAGS,
#                                       RELOPT, STRIP.
#   make                build fussy (dev build, -O2)
#   make test           build and run the test suite
#   make OPT="-O0 -g" DBG="-fsanitize=address,undefined"   sanitizer build
#   make release        clean optimized stripped binary
#   make install        install the release binary (PREFIX, DESTDIR honored)
#   make clean

CC      ?= cc
PREFIX  ?= /usr/local
BINDIR   = $(PREFIX)/bin
OPT     ?= -O2
DBG     ?=
RELOPT  ?= -O2 -DNDEBUG
STRIP   ?= strip

# _DEFAULT_SOURCE exposes POSIX-2008 + BSD extensions (SIGWINCH, strcasecmp,
# poll, clock_gettime, pthreads) on glibc; on macOS/FreeBSD we simply avoid
# defining _POSIX_C_SOURCE, which would otherwise hide those BSD extensions.
CSTD     = -std=c11 -D_DEFAULT_SOURCE
WARN     = -Wall -Wextra -Werror
CFLAGS   = $(CSTD) $(WARN) $(OPT) $(DBG) -Iinclude -Itest

# libgit2 flags are resolved by the shell at recipe time (backticks), which both
# BSD make and GNU make pass through verbatim - unlike $(shell ...)/!= which are
# flavor-specific. Override GIT2_CFLAGS/GIT2_LIBS to bypass pkg-config.
GIT2_CFLAGS ?= `pkg-config --cflags libgit2`
GIT2_LIBS   ?= `pkg-config --libs libgit2`

LDLIBS   = -lpthread

BIN      = fussy

# Object lists are explicit (no wildcard) so BSD make and GNU make agree.
LIBOBJS  = src/term.o src/util.o src/tree.o src/flatten.o src/git.o \
           src/render.o src/width.o src/app.o src/input.o src/fuzzy.o src/overlay.o src/proc.o
OBJS     = src/main.o $(LIBOBJS)

TESTBIN  = test/run
TESTOBJS = test/test_main.o test/test_util.o test/test_status.o \
           test/test_tree.o test/test_flatten.o test/test_git.o \
           test/test_render.o test/test_width.o test/test_key.o \
           test/test_nav.o test/test_irender.o test/test_input.o \
           test/test_fuzzy.o test/test_fuzzy_engine.o test/test_git_ops.o \
           test/test_overlay.o test/test_proc.o

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CFLAGS) $(GIT2_CFLAGS) -c $< -o $@

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(GIT2_LIBS) $(LDLIBS)

test: $(TESTBIN)
	./$(TESTBIN)

$(TESTBIN): $(TESTOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(TESTOBJS) $(LIBOBJS) $(GIT2_LIBS) $(LDLIBS)

clean:
	rm -f $(BIN) $(OBJS) $(TESTBIN) $(TESTOBJS)

# Clean optimized stripped binary. RELOPT/STRIP are overridable so packagers
# can inject their own flags or skip stripping (STRIP=: keeps symbols).
release:
	$(MAKE) clean
	$(MAKE) OPT="$(RELOPT)" $(BIN)
	$(STRIP) $(BIN)

# install always lays down a release build, never a stray debug/sanitizer one.
install: release
	mkdir -p $(DESTDIR)$(BINDIR)
	cp $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)

.PHONY: all test clean release install uninstall
