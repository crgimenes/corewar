# Redcode

Core War is a game between two programs loaded into the same memory.
They run side by side, one instruction each in turn, and each tries to
be the one still running when the other has stopped. The memory is
called the core, the machine that runs it is a MARS, and the language
the programs are written in is Redcode.

Nobody types the programs in while the fight runs. You write a warrior,
it is loaded somewhere in the core, and from then on it is on its own.

## The fight here

- The core is 8000 cells. In the arena, `M` switches it to 800 cells
  ("tiny") or 80 ("nano"), and `C` cuts the cycles a round lasts.
- Every cell starts as `DAT.F #0, #0`. A process that executes a DAT
  dies, so an untouched core is a minefield.
- Each warrior starts with one process, at its own first instruction.
  `SPL` makes more. A warrior is out when its last process dies.
- Two warriors fight by default and up to eight can, which is what the
  pick screen is for. They take turns: one instruction each, round and
  round. A cycle is one turn for everybody. A warrior with several
  processes runs them in turn, one per cycle, oldest first.
- A round ends when one warrior is left, or at 80000 cycles, which
  leaves it shared by whoever is still running. There are ten rounds,
  from positions drawn at random, never closer than 100 cells.
- The survivors of a round share `W * W - 1` points, where W is how many
  started it, and the division throws the remainder away. Two warriors:
  3 for the winner, 1 each when they both live. Three: 8 alone, 4 each
  when two live, 2 each when all three do. The arena shows the running
  score, and `corewar -b a.red b.red` prints it as text, with the same
  breakdown of rounds pMARS prints when more than two fight.
- A warrior is at most 100 instructions long (20 in a tiny core, 5 in a
  nano one).

## Every address is relative

An address in Redcode is an offset from the instruction being executed,
not a place in the core. `mov 0, 1` reads "this cell" and writes "the
next one". A label is turned into such an offset when the warrior is
assembled, which is why a warrior works wherever it is loaded, and why
a copy of it works too.

The core is a circle: after the last cell comes the first one. Every
address is taken modulo the core size, and negative offsets count
backwards.

## A warrior file

```
;redcode
;name Dwarf
;author A. K. Dewdney
;strategy Bombs every fourth cell and never leaves home.
bomb    dat #0, #0
dwarf   add #4, bomb
        mov bomb, @bomb
        jmp dwarf
        end dwarf
```

A line is `label opcode.modifier A-operand, B-operand ; comment`.
Everything but the opcode may be left out. Labels are case sensitive,
opcodes are not.

The lines that start with a semicolon are read before anything else:

- `;redcode` starts the warrior. Anything above it is thrown away, so a
  file can carry a letter, a listing, or a whole mail message on top.
- `;name` and `;author` are what the arena shows.
- `;assert expression` refuses to assemble unless the expression is
  true. It is how a warrior says what it needs, usually of the core:
  `;assert CORESIZE == 8000`.
- Any other `;` line is a comment. `;strategy` is the customary way of
  writing down what the thing is supposed to do.

`end` closes the file. `end label` also says where execution starts,
and so does `org label` anywhere above it. Without either, a warrior
starts at its first instruction.

## The instructions

Each one takes an A operand and a B operand. Read them as "from A to
B": `mov a, b` copies A to B, `add a, b` adds A to B.

- `dat` — kills the process that executes it. A DAT is also where a
  number is kept, since the fields are read without running it.
- `mov` — copies A to B. With `.i` it copies the whole instruction,
  which is how a warrior spreads.
- `add`, `sub`, `mul`, `div`, `mod` — arithmetic, result into B. A
  division or a remainder by zero kills the process.
- `jmp` — carry on at A.
- `jmz` — jump to A if B is zero. `jmn` — jump if it is not.
- `djn` — take one off B, then jump to A if what is left is not zero.
  This is the loop that counts down.
- `spl` — carry on where you were, and start a second process at A.
  Both belong to the same warrior and take turns.
- `slt` — skip the next instruction if A is less than B.
- `seq` (also spelled `cmp`) — skip if A and B are equal. `sne` — skip
  if they differ.
- `nop` — do nothing. Useful as a decoy or a hole.
- `ldp`, `stp` — read and write P-space, the private memory that
  survives between rounds.

## The modifiers

A modifier says which fields of the two instructions the opcode works
on. `mov.ab a, b` reads the A-field of A and writes it into B's B-field.

- `.a` — read A-field, write A-field.
- `.b` — read B-field, write B-field.
- `.ab` — read the A-field of the A-instruction, write the B-field of
  the B-instruction.
- `.ba` — the other way round.
- `.f` — both fields, A to A and B to B.
- `.x` — both fields, crossed: A to B and B to A.
- `.i` — the whole instruction, opcode and modes included. Only `mov`,
  `seq`, `sne` and `cmp` really use it; for arithmetic it behaves as
  `.f`.

Leave the modifier out and one is chosen for you, the way the standard
says:

- `dat` and `nop` take `.f`.
- `mov`, `seq`, `sne`, `cmp`: `.ab` when A is immediate, `.b` when B is
  immediate, and `.i` otherwise.
- `add`, `sub`, `mul`, `div`, `mod`: `.ab` when A is immediate, `.b`
  when B is immediate, `.f` otherwise.
- `slt`, `ldp`, `stp`: `.ab` when A is immediate, `.b` otherwise.
- `jmp`, `jmz`, `jmn`, `djn`, `spl` take `.b`.

Most warriors are written leaning on these, which is why a listing
often shows a modifier the source never mentions. When in doubt write
it out: `mov.i` means what it says in every core.

## The addressing modes

A mode goes in front of an operand and says how to reach the cell it
names.

- `#` immediate — the number is the value itself; the cell is the
  instruction being executed.
- `$` direct — the number is an offset to the cell. This is the default
  and is usually left out.
- `@` indirect — go to the cell at that offset and use its B-field as a
  further offset. `mov bomb, @bomb` drops the bomb wherever bomb's
  B-field is pointing.
- `<` predecrement — take one off that cell's B-field first, then use
  it. Sweeping backwards.
- `>` postincrement — use it, then add one. Sweeping forwards.
- `*`, `{`, `}` — the same three, reading the A-field instead of the
  B-field.

## Numbers, labels and EQU

Operands may be arithmetic: `+ - * / %`, comparisons `== != < > <= >=`,
`&&`, `||`, `!`, and parentheses. Labels are numbers too, so
`mov start, start+5` and `(last - first)` both work.

`EQU` gives a name to a piece of text, which is pasted in wherever the
name appears afterwards:

```
step    equ (CORESIZE / 3) + 1
        add #step, ptr
```

These names are already defined:

- `CORESIZE`, `MAXCYCLES`, `MAXPROCESSES`, `MAXLENGTH`, `MINDISTANCE` —
  the settings of the fight about to run.
- `ROUNDS`, `WARRIORS`, `PSPACESIZE`, `CURLINE`, `VERSION`.

A warrior that writes `CORESIZE / 4` instead of `2000` is one that
still makes sense in the tiny core.

`FOR` and `ROF` repeat a block, which is how a long row of identical
instructions is written without typing it out:

```
n       for 5
        dat #0, #0
        rof
```

The counter can be used inside the block, and `label&n` sticks the
counter onto a label so each turn of the loop gets its own.

## P-space

`PSPACESIZE` cells (a sixteenth of the core) belong to each warrior and
survive from one round to the next, which is how a warrior changes
tactics after losing. `stp` writes a cell, `ldp` reads one.

Cell 0 is written by the MARS at the end of every round: 0 if the
warrior lost, otherwise how many were left standing. Before the first
round it holds `CORESIZE - 1`.

`PIN`, which lets warriors share a P-space, is read and ignored here.

## The classic warriors

Five come with Core War, and the pick lists them as classics: `e N`
opens one in the editor, and on a desktop a copy of it lands in the
current directory first, yours to change.

### Imp

```
mov 0, 1
```

One instruction, which copies itself into the cell ahead and then runs
the copy. It walks the whole core, leaving a trail of itself, and there
is no way to shoot it from behind: what is behind it is already dead.
It cannot win either, since it never kills. Against every other classic
it plays for the tie, and usually gets it.

### Dwarf

```
bomb    dat #0, #0
dwarf   add #4, bomb
        mov bomb, @bomb
        jmp dwarf
```

It stays where it is and drops a DAT every fourth cell, for ever. Four
is chosen so the bombs never fall in the same place twice: the step has
to have no factor in common with the core size, and 8000 has only twos
and fives in it.

The weakness is in plain sight: its bomb is `DAT #0, #0`, which is
exactly what an untouched cell looks like. Anything that scans the core
for something that is not empty will walk straight past its work.

### Sweeper

```
sweep   mov bomb, >ptr
        jmp sweep
bomb    dat #0, #0
ptr     dat #0, 10
```

Instead of stepping across the core it fills it, one cell at a time,
with `>` moving the pointer along after each shot. Two cycles a cell,
so the whole core in sixteen thousand of them. It beats a bomber, which
is still stepping when the sweep arrives, and it ends by sweeping over
its own code — a real one stops just short of home.

### Scanner

```
step    equ 10
scan    seq @ptr, #0        ; is the cell it points at still empty?
        jmp hit             ; no: somebody lives there
        add #step, ptr      ; yes: look a little further on
        jmp scan
hit     mov bomb, @ptr
        add #step, ptr
        jmp scan
bomb    dat #0, #0
ptr     dat #0, 100
```

It looks before it shoots. `seq @ptr, #0` compares the B-field of the
cell it is pointing at with zero and skips the next instruction when
they match, so an empty cell means "carry on looking" and anything else
means "bomb it". One bomb per find is not much of an attack, and the
Dwarf's bombs are invisible to it, which is the point: it is here to
show the search, and making it dangerous is the exercise.

### Paper

```
step    equ (CORESIZE / 5)
size    equ (cnt - first + 1)
first   mov   #cnt+1-src, src   ; src just past my last instruction
        mov   #size, cnt        ; how many instructions to copy
loop    mov.i <src, <dst        ; backwards, one at a time
        djn.b loop, cnt
        spl   @dst              ; the copy runs too
        sub   #step, dst        ; the next one goes further off
        jmp   first
src     dat #0, #0
dst     dat #0, cnt+1+step-dst
cnt     dat #0, #0
```

It copies itself a fifth of the core away and starts the copy running,
then does it again, further off. The copies do the same, so there are
soon more of them than anything can shoot. It is copied backwards, from
the last instruction to the first, so that when the loop ends the
destination pointer is sitting on the copy's first instruction and
`spl @dst` can start it.

A bomb kills one copy. The others never notice.

## Strategies

The game has a rough triangle, and the three warriors above are its
corners.

- **Stone** — a bomber. Cheap, small, and deadly to anything that sits
  still or scans slowly.
- **Paper** — a replicator. Bombs cannot keep up with copies, so paper
  beats stone.
- **Scissors** — a scanner. It ignores empty core and shoots at what it
  finds, so it beats paper, which is all over the core and easy to
  find. Stone beats scissors, because a scanner is bigger and slower
  and gets bombed while it looks.

Things worth knowing when you write one:

- **The step matters.** A bombing run should have no factor in common
  with the core size, or it walks the same few cells for ever.
- **A DAT is not enough to kill.** It only kills the process that
  executes it. Against a warrior with a thousand processes, bombing one
  cell buys nothing; that is what `spl 0` bombs are for, which make the
  other side waste its turns.
- **Bombs that look like the core are invisible.** `dat #0, #0` cannot
  be told from an empty cell. Bomb with something else and a scanner
  will find your own work.
- **Decoys.** A row of harmless `dat` cells far from the code makes a
  scanner spend its cycles somewhere safe.
- **A core clear ends it.** Late in a round, sweeping the whole core is
  often the only way to finish a replicator off.
- **Watch the arena.** The panel on the right says who is alive, how
  much of the core each side has taken, and what would end the round.
  Slow the fight down with `-` and read the two instructions being
  executed.

## Where things are

- `corewar` opens the pick screen: the classics and your own `.red`
  files. `+ N` puts one in the fight (up to eight, so a room full of
  friends can watch their own fight it out), `-` takes the last one out
  again, `c` empties the list, `e N` opens one in the editor, `n name`
  starts a new one, `f` fights, `h` opens this manual.
- `corewar a.red b.red [more...]` goes straight to the arena, and with
  `-b` fights without the screen and prints the score. `-r N` sets the
  rounds, and `-F position` pins the second warrior, which only two of
  them can do.
