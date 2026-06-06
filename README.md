# fussC
(noun): fussy


A git-staging tree TUI written in C. Type a filename to jump to it in the tree, then stage, commit,
and sync without leaving the keyboard. A C port of `fuss` (Fortran) and `fussr` (Rust), built for speed.

Status: early development. See `.docs/plan.md` for the roadmap.

## Building

Requires a C11 compiler, `libgit2`, and POSIX threads. Works with `make` or `gmake`.

```sh
make                 # build the dev binary
./fussy
sudo make install    # install an optimized, stripped binary (PREFIX=/usr/local)
```

## Usage

```sh
fussy            # interactive tree of dirty files
fussy --all      # include all tracked files, dirty ones marked
fussy --print    # non-interactive tree output
fussy --help
```

### Controls

Type a filename to fuzzy-jump (the selection moves as you type; an idle pause resets the query).

Navigation: arrows or `Ctrl-N`/`Ctrl-P` move among siblings, `Right`/`Ctrl-F` enters a directory,
`Left`/`Ctrl-B` goes back up, `Space` expands or collapses in place, `H` toggles hidden files, `?`
shows the full keymap, `Q` quits.

Git commands are UPPERCASE (lowercase is the filter):

```
A stage        S stage-all      C commit    M amend
U unstage      Z unstage-all    X discard   D delete
R rename       T tag
```

`C`/`M` open a commit-message box (Enter commits, Esc cancels); `X`/`D` ask to confirm (y/n).

## Portability

Built and tested on FreeBSD 15 amd64, Linux, and macOS ARM64.

## Where the code lives

- `src/` - implementation, `include/` - headers, `test/` - the test suite.
- `.docs/` - architecture, audits of the original implementations, and the sprint plan.

## License

MIT. See `LICENSE`.
