# corewar: Core War on a terminal — the MARS (mars), the arena that shows
# the core (arena), the pick written in Filo (prog/, corewar.fbb), and the
# binary that carries them with the classic warriors and the manual.
CC ?= cc
CLANG_FORMAT ?= clang-format
CLANG_TIDY ?= clang-tidy
FILO_TERM ?= ../filo-term
FILO ?= ../clang_filo
# The compiler of the pick: C's, built from FILO, unless another is named.
# Go's filo writes the same bytes (make FILO_CLI=filo).
FILO_CLI ?= $(FILO)/build/filo
CLI_DEP = $(filter $(FILO)/build/filo,$(FILO_CLI))

WARN = -Wall -Wextra -Werror -Wshadow -Wconversion -Wdouble-promotion -Wundef
VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)
INC = -Isrc -Ibuild -I$(FILO_TERM)/src -I$(FILO)
FLAGS = -std=c11 -D_DEFAULT_SOURCE $(WARN) $(INC) -DCOREWAR_VERSION='"$(VERSION)"' -DFILO_VM_ONLY
TERMSRC = $(addprefix $(FILO_TERM)/src/,term.c canvas.c utf8.c keyin.c paint.c field.c app.c pager.c md.c hl.c tty.c)
FILOSRC = $(addprefix $(FILO)/,filo.c filo_math.c filo_strings.c filo_nolibc.c)
CORE = src/mars.c src/arena.c src/desk.c build/library.c $(TERMSRC) $(FILOSRC)
HDRS = $(wildcard src/*.h) $(wildcard $(FILO_TERM)/src/*.h) $(FILO)/filo.h
PROG = $(wildcard prog/*.filo)
WARRIORS = $(wildcard warriors/*.red)
# A release links everything it can: LDFLAGS=-static on Linux (musl); macOS
# has no static libc, and the binary needs nothing past libSystem anyway.
LDFLAGS ?=
PREFIX ?= /usr/local

.PHONY: all test fuzz fmt fmt-check tidy check qa smoke install dist clean

all: bin/corewar corewar.fbb

$(FILO)/build/filo:
	$(MAKE) -C $(FILO) build/filo

build/library.c: tools/library.sh redcode.md $(WARRIORS)
	@mkdir -p build
	sh tools/library.sh redcode.md $(WARRIORS) > $@

# What the binary gives the pick, listed by the binary's own code.
build/corewar.vm: $(CORE) $(FILO_TERM)/tools/appvm.c $(HDRS)
	$(CC) -O1 $(FLAGS) -o build/appvm $(CORE) $(FILO_TERM)/tools/appvm.c
	./build/appvm > $@

# One entry per file of prog/, one member, "corewar".
corewar.fbb: $(PROG) build/corewar.vm $(CLI_DEP)
	$(FILO_CLI) build -vm build/corewar.vm -o build/corewar.fbc $(PROG)
	$(FILO_CLI) bundle -o $@ build/corewar.fbc
	$(FILO_CLI) check -vm build/corewar.vm $@

build/corewar_fbb.c: corewar.fbb
	sh $(FILO_TERM)/tools/embed.sh corewar_fbb $< > $@

bin/corewar: $(CORE) build/corewar_fbb.c src/main.c $(HDRS)
	@mkdir -p bin
	$(CC) -O2 $(FLAGS) -o $@ $(CORE) build/corewar_fbb.c src/main.c $(LDFLAGS)

# The MARS against pMARS's own listings and results, and the pick and the
# arena driven as a terminal drives them, under the sanitizers.
test: $(CORE) build/corewar_fbb.c test/test_mars.c test/test_corewar.c $(HDRS)
	@mkdir -p build
	$(CC) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all $(FLAGS) \
		-o build/test_mars src/mars.c test/test_mars.c
	./build/test_mars
	$(CC) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all $(FLAGS) \
		-o build/test_corewar $(CORE) build/corewar_fbb.c test/test_corewar.c
	./build/test_corewar

# Any bytes as Redcode must assemble or be refused, never crash: a short
# run, a regression smoke test rather than a campaign. Needs a clang with
# libFuzzer (LLVM=/path/bin/ when the system's has none).
LLVM ?=
FUZZ_SECONDS ?= 60
fuzz: src/mars.c test/fuzz_mars.c test/redcode.dict
	@mkdir -p build
	$(LLVM)clang -std=c11 -g -O1 -Isrc -fsanitize=fuzzer,address,undefined \
		-o build/fuzz_mars src/mars.c test/fuzz_mars.c
	./build/fuzz_mars -max_total_time=$(FUZZ_SECONDS) -timeout=10 -max_len=2048 -dict=test/redcode.dict

SRC = src/*.c src/*.h test/*.c

fmt:
	$(CLANG_FORMAT) -i $(SRC)

fmt-check:
	$(CLANG_FORMAT) --dry-run --Werror $(SRC)

# The analyzer's insecureAPI check wants C11 Annex K (memcpy_s, snprintf_s),
# which no libc this builds on has: off, as in Filo's own gate.
TIDY_CHECKS = bugprone-*,cert-*,clang-analyzer-*,readability-*,-readability-magic-numbers,-readability-function-cognitive-complexity,-readability-identifier-length,-readability-braces-around-statements,-bugprone-easily-swappable-parameters,-cert-err33-c,-readability-else-after-return,-readability-avoid-nested-conditional-operator,-readability-math-missing-parentheses,-cert-dcl03-c,-readability-uppercase-literal-suffix,-clang-analyzer-optin.performance.Padding,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling

tidy: build/library.c
	$(CLANG_TIDY) --quiet --warnings-as-errors='*' --checks='$(TIDY_CHECKS)' \
		src/arena.c src/desk.c src/main.c test/test_corewar.c -- -std=c11 -D_DEFAULT_SOURCE $(INC)

check: build/library.c
	cppcheck --enable=warning,style,performance,portability --inline-suppr \
		--suppress=missingIncludeSystem --error-exitcode=1 $(INC) \
		src/arena.c src/desk.c src/main.c test/test_corewar.c

# The binary as shipped, on a terminal: it starts, draws, and quits.
smoke: bin/corewar
	sh $(FILO_TERM)/tools/smoke.sh bin/corewar '\033' q

qa: all fmt-check test smoke tidy check

install: bin/corewar
	mkdir -p $(PREFIX)/bin
	cp bin/corewar $(PREFIX)/bin/corewar

# What release.sh publishes (VERSION is its tag): corewar for macOS and Linux.
DIST_DIR ?= dist
dist: $(CORE) build/corewar_fbb.c
	sh $(FILO_TERM)/tools/dist.sh $(DIST_DIR) corewar -O2 $(FLAGS) \
		$(CORE) build/corewar_fbb.c src/main.c

clean:
	rm -rf build bin dist corewar.fbb
