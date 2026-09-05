#!/bin/sh
# Verifies that every public function and class of python/maelys_cli.py
# (top level, no leading underscore) is named in docs/python.md,
# the Python counterpart of scripts/api-doc-check.sh.
set -eu
module=python/maelys_cli.py
guide=docs/python.md
missing=0
for name in $(grep -oE '^(def|class) [A-Za-z][A-Za-z0-9_]*' "$module" | \
        sed -E 's/^(def|class) //' | grep -v '^_' | sort -u); do
    if ! grep -q "$name" "$guide"; then
        printf 'undocumented: %s (%s)\n' "$name" "$module" >&2
        missing=$((missing + 1))
    fi
done
if [ "$missing" -ne 0 ]; then
    printf '%s public name(s) of %s missing from %s\n' "$missing" "$module" "$guide" >&2
    exit 1
fi
printf 'python-doc-check: ok\n'
