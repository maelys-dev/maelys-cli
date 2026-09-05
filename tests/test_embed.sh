#!/bin/sh
set -eu

tool=$1
cc=${CC:-cc}
work=$(mktemp -d "${TMPDIR:-/tmp}/maelys-cli-embed.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

printf 'before @NAME@ after\n' >"$work/input"
printf '#!/bin/sh\n: >"%s"\n' "$work/executed" >"$work/probe"
chmod 0755 "$work/probe"
value="$work/probe|e
-e
s|nomatch|x"
"$tool" --define "NAME=$value" embedded="$work/input" >"$work/embedded.c"

printf '%s\n' '#include <stdio.h>' \
    'extern const char embedded[];' \
    'int main(void) { return fputs(embedded, stdout) < 0; }' >"$work/read.c"
"$cc" -std=c11 "$work/embedded.c" "$work/read.c" -o "$work/read"
"$work/read" >"$work/actual"
printf 'before %s after\n' "$value" >"$work/expected"
cmp "$work/expected" "$work/actual"
test ! -e "$work/executed"

if "$tool" --define 'BAD-NAME=value' embedded="$work/input" >/dev/null 2>&1; then
    exit 1
fi
if "$tool" --define NAME embedded="$work/input" >/dev/null 2>&1; then
    exit 1
fi
"$tool" --header embedded="$work/input" | grep -q '^extern const char embedded\[\];$'
printf '%s\n' 'test_embed: ok'
