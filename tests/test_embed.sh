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

# UTF-8 in an embedded text compiles under -Wconversion whatever the
# signedness of char, and reads back byte for byte. A bare 226 fits no
# signed char and a bare -30 no unsigned one; x86 and arm64 Linux differ,
# and CI found it twice on the agent texts before the tool cast the byte.
printf 'caf\303\251 \342\200\224 \303\240 ok\n' >"$work/utf8"
"$tool" utf8="$work/utf8" >"$work/utf8.c"
grep -q '(char)226' "$work/utf8.c"
printf '%s\n' '#include <stdio.h>' \
    'extern const char utf8[];' \
    'int main(void) { return fputs(utf8, stdout) < 0; }' >"$work/read8.c"
for signedness in -fsigned-char -funsigned-char; do
    "$cc" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Werror "$signedness" \
        "$work/utf8.c" "$work/read8.c" -o "$work/read8"
    "$work/read8" >"$work/actual8"
    cmp "$work/utf8" "$work/actual8"
done
printf '%s\n' 'test_embed: ok'
