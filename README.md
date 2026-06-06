# fussC
(noun): fussy


A git-staging tree TUI written in C. Type a filename to jump to it in the tree, then stage, commit,
and sync without leaving the keyboard. A C port of `fuss` (Fortran) and `fussr` (Rust), built for speed.

Status: early development. See `.docs/plan.md` for the roadmap.

## Building

Requires a C11 compiler, `libgit2`, and POSIX threads. Works with `make` or `gmake`.

```sh
make
./fussy
```

## Usage

```sh
fussy            # interactive tree of dirty files
fussy --all      # include all tracked files, dirty ones marked
fussy --print    # non-interactive tree output
fussy --help
```

Interactive keys (in progress): `j`/`k` move, arrows expand/collapse and traverse, type a name to
fuzzy-jump, and a git mode for stage/unstage/commit/push/pull.

## Portability

Built and tested on FreeBSD 15 amd64, Linux, and macOS ARM64.

## Where the code lives

- `src/` - implementation, `include/` - headers, `test/` - the test suite.
- `.docs/` - architecture, audits of the original implementations, and the sprint plan.

## License

MIT. See `LICENSE`.
