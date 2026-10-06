# corewar

Core War on a terminal. Warriors written in Redcode fight in one core:
two of them, or up to eight at once. A MARS compatible with pMARS
(ICWS'94, P-space, FOR/ROF), an arena that shows the core while they
fight, and a pick, written in [Filo](https://github.com/crgimenes/clang_filo),
to choose who fights. Five classic warriors and the Redcode manual come
inside the binary.

```
corewar                         # the pick: your .red files and the classics
corewar imp.red dwarf.red       # straight to the arena
corewar -b -r 100 a.red b.red   # no screen: the score as pMARS prints it
```

In the arena: space pauses, `+` and `-` change the speed, `n` ends the
round, `r` starts the match again, `m` changes the core size, `c` the
round's length, Esc leaves. In the pick, `h` opens the manual and
`e N` opens a warrior in `$VISUAL`, `$EDITOR` or `vi`.

## How it is made

| file | what it is |
| --- | --- |
| `src/mars.c` | the MARS: assembler and machine, no terminal, no host |
| `src/arena.c` | the fight on a terminal, over [filo-term](https://github.com/crgimenes/filo-term)'s canvas; files and leaving are the host's |
| `prog/*.filo` | the pick, `corewar.fbb`: the same program wherever Core War runs, talking to the host only through `cw-*` builtins |
| `src/desk.c`, `src/main.c` | the desktop's host: the directory, the carried classics, the pager, the editor |

The pick asks the host for the warriors on offer (`cw-library`), the
fight (`cw-fight`), an editor (`cw-edit`) and the manual (`cw-manual`),
and keeps the fighters with `cw-count`, `cw-picked`, `cw-colour`,
`cw-add`, `cw-drop` and `cw-clear`. A host that offers those runs the
same `corewar.fbb`.

## Build

```
make            # bin/corewar and corewar.fbb
make test       # the MARS against pMARS's listings and results, and the
                # pick and the arena driven as a terminal drives them
make qa         # the above, clang-format, clang-tidy, cppcheck
```

`FILO_TERM ?= ../filo-term`, `FILO ?= ../clang_filo`. The pick is compiled
by the filo CLI, C's built from `FILO` unless another is named: Go's writes
the same bytes (`make FILO_CLI=filo`). A release links statically on Linux
(`make LDFLAGS=-static` on musl).

## License

MIT, except `test/pmars/`: test data from the pMARS 0.9.2 distribution,
under the GNU GPL v2 (`test/pmars/COPYING`, `test/pmars/NOTICE`), used to
hold the MARS to pMARS and not part of the program.
