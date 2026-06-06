# fussC
(noun): fussy


A git-staging tree TUI written in C. Type a filename to jump to it in the tree, then stage, commit,
and sync without leaving the keyboard. A C port of `fuss` (Fortran) and `fussr` (Rust), built for speed.

Status: early development. See `.docs/plan.md` for the roadmap.

## Building

Requires a C11 compiler, `libgit2`, and POSIX threads. Works with `make` or `gmake`.
The full-screen commit viewer uses [paige](https://github.com/tenseleyFlow/paige), a
bundled submodule, so clone recursively (or init it after the fact):

```sh
git clone --recursive <repo>          # or: git submodule update --init
make                 # build the dev binary
./fussy
make test            # build and run the test suite
make DEBUG=1 test    # ASan + UBSan build of the suite
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
R rename       T tag            V view      G status
P push         L pull           F fetch     B log
```

`B` opens the commit-history browser: a fuzzy list of commits with a `git show`
preview pane; type to filter, Enter opens the chosen commit in `$PAGER`.

`C`/`M` open a commit-message box (Enter commits, Esc cancels); `X`/`D` ask to confirm (y/n).
`T` takes a tag name then an optional message (empty = lightweight, otherwise annotated).
`V` shows the selected file's diff (or its contents if unchanged) and `G` the full status, both in
`$PAGER`. `P`/`L`/`F` are push/pull/fetch via `git`; with more than one remote and no upstream set,
a picker appears. Push uses `--follow-tags`, so annotated tags ship with the branch.

## Portability

Built and tested on FreeBSD 15 amd64, Linux, and macOS ARM64.

## Where the code lives

- `src/` - implementation, `include/` - headers, `test/` - the test suite.
- `.docs/` - architecture, audits of the original implementations, and the sprint plan.

## License

MIT. See `LICENSE`.
