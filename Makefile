SHELL := /bin/sh

CC ?= cc
CXX ?= c++
AR ?= ar
PREFIX ?= /usr/local
DESTDIR ?=
BUILD ?= build/release

CPPFLAGS ?=
CFLAGS ?= -O2 -g
CXXFLAGS ?= -O2 -g
WARNINGS := -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes -Wformat=2
COMMON_CPPFLAGS := -Iinclude -Isrc -D_POSIX_C_SOURCE=200809L \
	-D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE \
	-DMAELYS_CLI_COMMANDS_DIR='"$(PREFIX)/share/maelys/commands"'
COMMON_CFLAGS := -std=c11 $(WARNINGS)
COMMON_CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -Werror

VERSION := $(shell sed -n '1p' VERSION)

# maelys-json is the JSON reader of the framework: a sibling checkout built
# on demand, or an installed copy (MAELYS_JSON_CFLAGS/MAELYS_JSON_LIBS from
# pkg-config). Pin: tag v0.1.0.
MAELYS_JSON_DIR ?= ../maelys-json
MAELYS_JSON_LIB ?= $(MAELYS_JSON_DIR)/build/lib/libmaelys-json.a
MAELYS_JSON_CFLAGS ?= -I$(MAELYS_JSON_DIR)/include
MAELYS_JSON_LIBS ?= $(MAELYS_JSON_LIB)
HEADERS := $(wildcard include/maelys/*.h include/maelys/cli/*.h src/*.h \
	cmd/maelys/*.h tests/*.h)

SOURCES := src/version.c src/values.c src/environment.c src/files.c \
	src/digest.c src/json.c src/terminal.c src/process.c src/catalog.c \
	src/invocation.c src/app.c
OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(SOURCES))
LIB := $(BUILD)/lib/libmaelys_cli.a
# Manifest discovery reads untrusted JSON through maelys-json; it lives in
# its own archive so the core stays dependency-free.
EXTENSION_SOURCES := src/extension.c
EXTENSION_OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(EXTENSION_SOURCES))
EXTENSION_LIB := $(BUILD)/lib/libmaelys_cli_extension.a

EMBED := tools/maelys-cli-embed
AGENT_TEXTS := share/agents/instructions-block.md share/agents/maelys-cli-guide.md \
	share/agents/claude-skill.md
EMBEDDED := $(BUILD)/generated/agent_texts.c
HELLO_SCHEMAS := $(wildcard examples/hello/schemas/*.json)
HELLO_SCHEMA_SYMBOLS := $(foreach schema,$(HELLO_SCHEMAS),\
	hello_$(subst -,_,$(basename $(notdir $(schema))))_schema=$(schema))
HELLO_GENERATED := $(BUILD)/generated/hello_schemas.c $(BUILD)/generated/hello_schemas.h
DISPATCHER_SOURCES := cmd/maelys/main.c cmd/maelys/agents.c
DISPATCHER_OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(DISPATCHER_SOURCES)) \
	$(BUILD)/generated/agent_texts.o
DISPATCHER := $(BUILD)/bin/maelys
EXAMPLE := $(BUILD)/bin/maelys-hello

TEST_NAMES := test_values test_json test_files test_digest test_process \
	test_catalog test_app test_extension
TESTS := $(addprefix $(BUILD)/tests/,$(TEST_NAMES))
HEADER_CPP := $(BUILD)/tests/header_cpp
PC := $(BUILD)/pkgconfig/maelys-cli.pc
EXTENSION_PC := $(BUILD)/pkgconfig/maelys-cli-extension.pc

.PHONY: all check test header-check check-version cli-check api-doc-check agent-doc-check doc-topics-check install \
	install-check uninstall dist clean asan-ubsan analyze cmake-check \
	generate-cli-reference contract-check agents-install

all: $(LIB) $(EXTENSION_LIB) $(DISPATCHER) $(EXAMPLE) $(PC) $(EXTENSION_PC)

$(BUILD)/%.o: %.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_CPPFLAGS) $(CFLAGS) $(COMMON_CFLAGS) -c $< -o $@

$(LIB): $(OBJECTS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

$(MAELYS_JSON_DIR)/build/lib/libmaelys-json.a:
	$(MAKE) -C $(MAELYS_JSON_DIR)

$(BUILD)/src/extension.o: src/extension.c $(HEADERS) $(MAELYS_JSON_LIB)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_CPPFLAGS) $(MAELYS_JSON_CFLAGS) $(CFLAGS) $(COMMON_CFLAGS) -c $< -o $@

$(EXTENSION_LIB): $(EXTENSION_OBJECTS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

$(EMBEDDED): $(AGENT_TEXTS) $(EMBED) VERSION
	@mkdir -p $(@D)
	{ printf 'const char maelys_agents_version[] = "%s";\n' "$(VERSION)"; \
	  $(EMBED) --define VERSION=$(VERSION) \
	    maelys_agents_instructions_block=share/agents/instructions-block.md \
	    maelys_agents_guide=share/agents/maelys-cli-guide.md \
	    maelys_agents_claude_skill=share/agents/claude-skill.md; } > $@.tmp
	mv $@.tmp $@

# JSON Schemas of the example are ordinary files embedded at build time.
$(BUILD)/generated/hello_schemas.c: $(HELLO_SCHEMAS) $(EMBED)
	@mkdir -p $(@D)
	$(EMBED) $(HELLO_SCHEMA_SYMBOLS) > $@.tmp
	mv $@.tmp $@

$(BUILD)/generated/hello_schemas.h: $(HELLO_SCHEMAS) $(EMBED)
	@mkdir -p $(@D)
	$(EMBED) --header $(HELLO_SCHEMA_SYMBOLS) > $@.tmp
	mv $@.tmp $@

$(BUILD)/generated/agent_texts.o: $(EMBEDDED)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_CPPFLAGS) $(CFLAGS) $(COMMON_CFLAGS) -c $< -o $@

$(BUILD)/cmd/maelys/%.o: cmd/maelys/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_CPPFLAGS) $(CFLAGS) $(COMMON_CFLAGS) -c $< -o $@

$(DISPATCHER): $(DISPATCHER_OBJECTS) $(EXTENSION_LIB) $(LIB) $(MAELYS_JSON_LIB)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(COMMON_CFLAGS) $(DISPATCHER_OBJECTS) $(EXTENSION_LIB) $(LIB) $(MAELYS_JSON_LIBS) $(LDFLAGS) -o $@

# The example is a plain product: core archive only, no maelys-json.
$(EXAMPLE): examples/hello/main.c $(HELLO_GENERATED) $(LIB) $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_CPPFLAGS) -I$(BUILD)/generated $(CFLAGS) $(COMMON_CFLAGS) \
		$< $(BUILD)/generated/hello_schemas.c $(LIB) $(LDFLAGS) -o $@

$(BUILD)/tests/test_extension: tests/test_extension.c $(EXTENSION_LIB) $(LIB) $(MAELYS_JSON_LIB) $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_CPPFLAGS) $(CFLAGS) $(COMMON_CFLAGS) $< $(EXTENSION_LIB) $(LIB) $(MAELYS_JSON_LIBS) $(LDFLAGS) -o $@

$(BUILD)/tests/test_%: tests/test_%.c $(LIB) $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_CPPFLAGS) $(CFLAGS) $(COMMON_CFLAGS) $< $(LIB) $(LDFLAGS) -o $@

$(HEADER_CPP): tests/header_cpp.cpp $(LIB)
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(COMMON_CPPFLAGS) $(CXXFLAGS) $(COMMON_CXXFLAGS) $< -c -o $@.o
	$(CXX) $@.o $(LIB) $(LDFLAGS) -o $@

$(BUILD)/pkgconfig/%.pc: pkgconfig/%.pc.in VERSION
	@mkdir -p $(@D)
	sed -e 's|@PREFIX@|$(PREFIX)|g' -e 's|@VERSION@|$(VERSION)|g' $< > $@

test: $(TESTS)
	@for test in $(TESTS); do echo "== $$test"; $$test || exit 1; done

cli-check: $(DISPATCHER) $(EXAMPLE)
	./tests/test_cli.sh $(BUILD)/bin

header-check: $(HEADER_CPP)
	$(HEADER_CPP)

check-version:
	@test "$(VERSION)" = "$$(sed -n 's/^#define MAELYS_CLI_VERSION "\([^"]*\)"/\1/p' include/maelys/cli/version.h)"
	@grep -q "^## $(VERSION)" CHANGELOG.md

api-doc-check:
	./scripts/api-doc-check.sh

agent-doc-check:
	./scripts/agent-doc-check.sh

doc-topics-check:
	./scripts/doc-topics-check.sh

# contract-check needs python3; it is part of check wherever python3 exists.
check: test cli-check header-check check-version api-doc-check agent-doc-check doc-topics-check
	@if command -v python3 >/dev/null 2>&1; then $(MAKE) contract-check; \
	else echo "contract-check: skipped (python3 not found)"; fi

asan-ubsan:
	$(MAKE) check BUILD=build/asan-ubsan CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined'

analyze: $(HELLO_GENERATED)
	@for source in $(SOURCES) $(EXTENSION_SOURCES) $(DISPATCHER_SOURCES) examples/hello/main.c; do \
		$(CC) --analyze -Xanalyzer -analyzer-output=text \
			$(CPPFLAGS) $(COMMON_CPPFLAGS) $(MAELYS_JSON_CFLAGS) -I$(BUILD)/generated -std=c11 $$source || exit 1; \
	done

generate-cli-reference: $(DISPATCHER) $(EXAMPLE)
	python3 tools/generate_cli_reference.py --build $(BUILD)/bin \
		--markdown docs/cli-reference.md --json docs/cli-contract.json

# Proves that the committed reference and contract match what the binaries
# describe. Run before a release and in CI; needs python3.
contract-check: $(DISPATCHER) $(EXAMPLE)
	@mkdir -p $(BUILD)/contract
	python3 tools/generate_cli_reference.py --build $(BUILD)/bin \
		--markdown $(BUILD)/contract/cli-reference.md \
		--json $(BUILD)/contract/cli-contract.json
	cmp -s $(BUILD)/contract/cli-reference.md docs/cli-reference.md || \
		{ echo "docs/cli-reference.md drifted; run make generate-cli-reference" >&2; exit 1; }
	cmp -s $(BUILD)/contract/cli-contract.json docs/cli-contract.json || \
		{ echo "docs/cli-contract.json drifted; run make generate-cli-reference" >&2; exit 1; }
	@echo "contract-check: ok"

install: $(LIB) $(EXTENSION_LIB) $(DISPATCHER) $(PC) $(EXTENSION_PC)
	install -d $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/bin \
		$(DESTDIR)$(PREFIX)/include/maelys/cli \
		$(DESTDIR)$(PREFIX)/lib/pkgconfig \
		$(DESTDIR)$(PREFIX)/share/maelys-cli/agents \
		$(DESTDIR)$(PREFIX)/share/maelys-cli/docs \
		$(DESTDIR)$(PREFIX)/share/maelys/commands
	install -m 0644 $(LIB) $(DESTDIR)$(PREFIX)/lib/libmaelys_cli.a
	install -m 0644 $(EXTENSION_LIB) $(DESTDIR)$(PREFIX)/lib/libmaelys_cli_extension.a
	install -m 0644 $(EXTENSION_PC) $(DESTDIR)$(PREFIX)/lib/pkgconfig/maelys-cli-extension.pc
	install -m 0755 $(DISPATCHER) $(DESTDIR)$(PREFIX)/bin/maelys
	install -m 0755 $(EMBED) $(DESTDIR)$(PREFIX)/bin/maelys-cli-embed
	install -m 0755 tools/generate_cli_reference.py $(DESTDIR)$(PREFIX)/bin/maelys-cli-reference
	install -m 0644 include/maelys/cli.h $(DESTDIR)$(PREFIX)/include/maelys/cli.h
	install -m 0644 include/maelys/cli/*.h $(DESTDIR)$(PREFIX)/include/maelys/cli/
	install -m 0644 $(PC) $(DESTDIR)$(PREFIX)/lib/pkgconfig/maelys-cli.pc
	install -m 0644 share/agents/*.md $(DESTDIR)$(PREFIX)/share/maelys-cli/agents/
	install -d $(DESTDIR)$(PREFIX)/share/maelys-cli/templates
	install -m 0644 share/templates/* $(DESTDIR)$(PREFIX)/share/maelys-cli/templates/
	install -m 0644 share/LICENSE $(DESTDIR)$(PREFIX)/share/maelys-cli/LICENSE
	install -m 0644 LICENSE LICENSING.md $(DESTDIR)$(PREFIX)/share/maelys-cli/docs/
	install -m 0644 docs/*.md $(DESTDIR)$(PREFIX)/share/maelys-cli/docs/

install-check: all
	./scripts/install-check.sh

# Builds and installs through CMake into a scratch prefix, then configures a
# consumer with find_package(maelys-cli).
cmake-check:
	./scripts/cmake-check.sh

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/lib/libmaelys_cli.a \
		$(DESTDIR)$(PREFIX)/lib/libmaelys_cli_extension.a \
		$(DESTDIR)$(PREFIX)/lib/pkgconfig/maelys-cli-extension.pc \
		$(DESTDIR)$(PREFIX)/bin/maelys \
		$(DESTDIR)$(PREFIX)/bin/maelys-cli-embed \
		$(DESTDIR)$(PREFIX)/bin/maelys-cli-reference \
		$(DESTDIR)$(PREFIX)/include/maelys/cli.h \
		$(DESTDIR)$(PREFIX)/lib/pkgconfig/maelys-cli.pc
	rm -rf $(DESTDIR)$(PREFIX)/include/maelys/cli \
		$(DESTDIR)$(PREFIX)/share/maelys-cli

# Install the agent instructions into a consumer project:
#   make agents-install PROJECT=/path/to/project
agents-install: $(DISPATCHER)
	@test -n "$(PROJECT)" || { echo "usage: make agents-install PROJECT=DIR"; exit 64; }
	$(DISPATCHER) agents install "$(PROJECT)" --apply

dist: check
	@mkdir -p dist
	git archive --format=tar --prefix=maelys-cli-$(VERSION)/ HEAD | \
		gzip -n > dist/maelys-cli-$(VERSION).tar.gz

clean:
	rm -rf build
