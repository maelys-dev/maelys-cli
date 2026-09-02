#!/bin/sh
set -eu

root=$(mktemp -d "${TMPDIR:-/tmp}/maelys-cli-install.XXXXXX")
cleanup() {
    rm -rf "$root"
}
trap cleanup EXIT HUP INT TERM

make install DESTDIR="$root" PREFIX=/usr >/dev/null
test -f "$root/usr/lib/libmaelys_cli.a"
test -x "$root/usr/bin/maelys"
test -x "$root/usr/bin/maelys-cli-embed"
test -f "$root/usr/include/maelys/cli.h"
test -f "$root/usr/include/maelys/cli/app.h"
test -f "$root/usr/lib/pkgconfig/maelys-cli.pc"
test -f "$root/usr/share/maelys-cli/agents/instructions-block.md"
test -d "$root/usr/share/maelys/commands"
test -f "$root/usr/share/maelys-cli/templates/command-conventions.md"
test -f "$root/usr/share/maelys-cli/templates/agent-cli.md"

printf '{"type":"object","required":["ok"]}\n' > "$root/hello.json"
"$root/usr/bin/maelys-cli-embed" hello_schema="$root/hello.json" > "$root/schemas.c"
cat > "$root/smoke.c" <<'SMOKE'
#include <maelys/cli.h>
extern const char hello_schema[];
static int hello(maelys_cli_context_t *context) {
    return maelys_cli_succeed(context, "{\"ok\":true}", "hello", MAELYS_CLI_EXIT_OK);
}
static const maelys_cli_command_t commands[] = {
    {MAELYS_CLI_READ("hello", "hello", "Say hello.", hello),
     MAELYS_CLI_SCHEMA(hello_schema)},
};
int main(int argc, char **argv) {
    static const maelys_cli_app_t app = {
        .program = "smoke", .product = "Smoke", .version = "0.0.1",
        .commands = commands, .command_count = 1u,
    };
    return maelys_cli_main(&app, argc, argv);
}
SMOKE

${CC:-cc} -std=c11 -Wall -Wextra -Wpedantic -Werror -I"$root/usr/include" \
    "$root/smoke.c" "$root/schemas.c" "$root/usr/lib/libmaelys_cli.a" -o "$root/smoke"
test "$("$root/smoke" hello)" = "hello"
"$root/smoke" hello --json --compact | grep -q '"command":"hello","ok":true'
"$root/smoke" describe hello --json --compact | grep -q '"outputSchema":{"type":"object","required":\["ok"\]}'

project="$root/project"
mkdir -p "$project"
"$root/usr/bin/maelys" agents install "$project" --apply >/dev/null
test -f "$project/AGENTS.md"
test -f "$project/CLAUDE.md"
test -f "$project/docs/maelys-cli-guide.md"
printf '%s\n' "install check: ok"
