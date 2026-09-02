#!/bin/sh
# Verifies that the installed agent guide names every declaration macro of
# catalog.h and every handler-facing function of app.h, so the texts copied
# into consumer projects cannot lag behind the public surface.
set -eu
guide=share/agents/maelys-cli-guide.md
missing=0
for macro in $(grep -oE '^#define MAELYS_CLI_[A-Z0-9_]+\(' include/maelys/cli/catalog.h | \
        sed 's/^#define //; s/($//' | grep -vE '^MAELYS_CLI_(COUNT|OPERANDS|OPTIONS)$' | sort -u); do
    if ! grep -q "$macro" "$guide"; then
        printf 'agent guide lacks macro %s\n' "$macro" >&2
        missing=$((missing + 1))
    fi
done
for function in $(tr '\n' ' ' < include/maelys/cli/app.h | \
        grep -oE 'maelys_cli_[a-z0-9_]+ *\(' | sed 's/ *($//' | sort -u | \
        grep -vE '^maelys_cli_(main|run|catalog_validate|builtin_commands)$'); do
    if ! grep -q "$function" "$guide"; then
        printf 'agent guide lacks accessor %s\n' "$function" >&2
        missing=$((missing + 1))
    fi
done
if [ "$missing" -ne 0 ]; then
    printf '%s public item(s) missing from %s\n' "$missing" "$guide" >&2
    exit 1
fi
printf 'agent-doc-check: ok\n'
