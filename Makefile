# fussC - portable Makefile (works with BSD make and GNU make).
# Knobs (override on the command line): CC, OPT, DBG, PREFIX, CFLAGS, LDFLAGS,
#                                       RELOPT, STRIP, DEBUG.
#   make                build fussy (dev build, -O2)
#   make test           build and run the test suite
#   make DEBUG=1 test    ASan+UBSan build of the suite (the common debug build)
#   make OPT="-O1 -g" DBG="-fsanitize=thread" test   explicit flags (e.g. TSan)
#   make release         clean optimized stripped binary
#   make install         install the release binary (PREFIX, DESTDIR honored)
#   make clean

CC      ?= cc
PREFIX  ?= /usr/local
BINDIR   = $(PREFIX)/bin

# DEBUG=1 selects a sanitizer build without spelling out the flags. Selection is
# by variable indirection - OPT/DBG default to OPT_$(DEBUG)/DBG_$(DEBUG), so an
# empty DEBUG picks OPT_/DBG_ and DEBUG=1 picks OPT_1/DBG_1. This works in both
# BSD make and GNU make (plain expansion, no non-portable .if/ifeq). Explicit
# OPT=/DBG= on the command line still win via ?=. For TSan use the explicit form.
DEBUG   ?=
OPT_     = -O2
OPT_1    = -O0 -g
DBG_     =
DBG_1    = -fsanitize=address,undefined
OPT     ?= $(OPT_$(DEBUG))
DBG     ?= $(DBG_$(DEBUG))
RELOPT  ?= -O2 -DNDEBUG
STRIP   ?= strip

# _DEFAULT_SOURCE exposes POSIX-2008 + BSD extensions (SIGWINCH, strcasecmp,
# poll, clock_gettime, pthreads) on glibc; on macOS/FreeBSD we simply avoid
# defining _POSIX_C_SOURCE, which would otherwise hide those BSD extensions.
CSTD     = -std=c11 -D_DEFAULT_SOURCE
WARN     = -Wall -Wextra -Werror
CFLAGS   = $(CSTD) $(WARN) $(OPT) $(DBG) -Iinclude -Itest -Ipaige/include

# paige is a git submodule (a bespoke pager engine) built by its own portable
# Makefile into a static lib; we link it into fussy for the full-screen commit
# view. Built via sub-make so paige owns its own flags/feature macros.
PAIGE_LIB = paige/build/libpaige.a

# libgit2 flags are resolved by the shell at recipe time (backticks), which both
# BSD make and GNU make pass through verbatim - unlike $(shell ...)/!= which are
# flavor-specific. Override GIT2_CFLAGS/GIT2_LIBS to bypass pkg-config.
GIT2_CFLAGS ?= `pkg-config --cflags libgit2`
GIT2_LIBS   ?= `pkg-config --libs libgit2`

LDLIBS   = -lpthread

BIN      = fussy

# Object lists are explicit (no wildcard) so BSD make and GNU make agree.
LIBOBJS  = src/term.o src/util.o src/tree.o src/flatten.o src/git.o \
           src/render.o src/width.o src/app.o src/input.o src/fuzzy.o src/overlay.o src/proc.o \
           src/strbuf.o src/picker.o
OBJS     = src/main.o $(LIBOBJS)

TESTBIN  = test/run
TESTOBJS = test/test_main.o test/test_util.o test/test_status.o \
           test/test_tree.o test/test_flatten.o test/test_git.o \
           test/test_render.o test/test_width.o test/test_key.o \
           test/test_nav.o test/test_irender.o test/test_input.o \
           test/test_fuzzy.o test/test_fuzzy_engine.o test/test_git_ops.o \
           test/test_overlay.o test/test_proc.o test/test_gitnet.o \
           test/test_e2e.o test/test_picker.o

# Headers listed explicitly (same reason as the object lists). The per-object
# rules near the end pin each object to all of them.
HEADERS  = include/app.h include/flatten.h include/fussy.h include/fuzzy.h \
           include/git.h include/input.h include/overlay.h include/proc.h \
           include/picker.h include/render.h include/status.h include/strbuf.h \
           include/term.h include/tree.h include/util.h include/width.h test/test.h

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CFLAGS) $(GIT2_CFLAGS) -c $< -o $@

all: $(BIN)

$(BIN): $(OBJS) $(PAIGE_LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(PAIGE_LIB) $(GIT2_LIBS) $(LDLIBS)

$(PAIGE_LIB): paige/include/paige.h paige/Makefile
	$(MAKE) -C paige build/libpaige.a

test: $(TESTBIN)
	./$(TESTBIN)

$(TESTBIN): $(TESTOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(TESTOBJS) $(LIBOBJS) $(GIT2_LIBS) $(LDLIBS)

# --- fuzzing (clang + libFuzzer only; not part of the portable build) ---------
# `make fuzz` builds the harnesses; each binary then runs as `./fuzz/fuzz_NAME
# fuzz/corpus_NAME [-max_total_time=N]`. Recipes use explicit source lists (no
# $^), so the syntax is fine under both makes even though the toolchain is clang.
FUZZ_CC    ?= clang
FUZZ_FLAGS  = -std=c11 -D_DEFAULT_SOURCE -g -O1 \
              -fsanitize=fuzzer,address,undefined -Iinclude $(GIT2_CFLAGS)
# width.c pulls in clip_to_width -> strbuf, so the scorer fuzzer links strbuf.c.
SCORE_SRC   = fuzz/fuzz_score.c src/fuzzy.c src/tree.c src/util.c src/width.c \
              src/strbuf.c
PATH_SRC    = fuzz/fuzz_path.c src/tree.c src/util.c
KEY_SRC     = fuzz/fuzz_key.c src/term.c
FUZZERS     = fuzz/fuzz_score fuzz/fuzz_path fuzz/fuzz_key

fuzz: $(FUZZERS)

fuzz/fuzz_score: $(SCORE_SRC)
	$(FUZZ_CC) $(FUZZ_FLAGS) -o $@ $(SCORE_SRC) -lpthread
fuzz/fuzz_path: $(PATH_SRC)
	$(FUZZ_CC) $(FUZZ_FLAGS) -o $@ $(PATH_SRC)
fuzz/fuzz_key: $(KEY_SRC)
	$(FUZZ_CC) $(FUZZ_FLAGS) -o $@ $(KEY_SRC)

clean:
	rm -f $(BIN) $(OBJS) $(TESTBIN) $(TESTOBJS) $(FUZZERS)
	-$(MAKE) -C paige clean

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

.PHONY: all test clean release install uninstall fuzz

# Per-object header deps, after the goals so `all` stays the default. Coarse
# (every object vs every header) but correct and portable: each line names its
# source so both makes still apply the .c.o rule. A bare `obj: $(HEADERS)`
# drops the source prereq and breaks the implicit rule.
src/main.o: src/main.c $(HEADERS) paige/include/paige.h
src/term.o: src/term.c $(HEADERS)
src/util.o: src/util.c $(HEADERS)
src/tree.o: src/tree.c $(HEADERS)
src/flatten.o: src/flatten.c $(HEADERS)
src/git.o: src/git.c $(HEADERS)
src/render.o: src/render.c $(HEADERS)
src/width.o: src/width.c $(HEADERS)
src/app.o: src/app.c $(HEADERS)
src/input.o: src/input.c $(HEADERS)
src/fuzzy.o: src/fuzzy.c $(HEADERS)
src/overlay.o: src/overlay.c $(HEADERS)
src/proc.o: src/proc.c $(HEADERS)
src/strbuf.o: src/strbuf.c $(HEADERS)
src/picker.o: src/picker.c $(HEADERS)
test/test_main.o: test/test_main.c $(HEADERS)
test/test_util.o: test/test_util.c $(HEADERS)
test/test_status.o: test/test_status.c $(HEADERS)
test/test_tree.o: test/test_tree.c $(HEADERS)
test/test_flatten.o: test/test_flatten.c $(HEADERS)
test/test_git.o: test/test_git.c $(HEADERS)
test/test_render.o: test/test_render.c $(HEADERS)
test/test_width.o: test/test_width.c $(HEADERS)
test/test_key.o: test/test_key.c $(HEADERS)
test/test_nav.o: test/test_nav.c $(HEADERS)
test/test_irender.o: test/test_irender.c $(HEADERS)
test/test_input.o: test/test_input.c $(HEADERS)
test/test_fuzzy.o: test/test_fuzzy.c $(HEADERS)
test/test_fuzzy_engine.o: test/test_fuzzy_engine.c $(HEADERS)
test/test_git_ops.o: test/test_git_ops.c $(HEADERS)
test/test_overlay.o: test/test_overlay.c $(HEADERS)
test/test_proc.o: test/test_proc.c $(HEADERS)
test/test_gitnet.o: test/test_gitnet.c $(HEADERS)
test/test_e2e.o: test/test_e2e.c $(HEADERS)
test/test_picker.o: test/test_picker.c $(HEADERS)
