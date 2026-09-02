#!/bin/sh
# Verifies that every public function declared in include/maelys/cli/*.h is
# named in docs/api-reference.md.
set -eu
missing=0
for header in include/maelys/cli/*.h; do
    for function in $(tr '\n' ' ' < "$header" | \
            grep -oE 'maelys_cli_[a-z0-9_]+ *\(' | \
            sed 's/ *($//' | sort -u); do
        if ! grep -q "$function" docs/api-reference.md; then
            printf 'undocumented: %s (%s)\n' "$function" "$header" >&2
            missing=$((missing + 1))
        fi
    done
done
if [ "$missing" -ne 0 ]; then
    printf '%s public function(s) missing from docs/api-reference.md\n' "$missing" >&2
    exit 1
fi
printf 'api-doc-check: ok\n'
