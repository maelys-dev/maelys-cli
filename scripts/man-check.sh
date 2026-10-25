#!/bin/sh
# Renders the manual page of the reference programs with tools/maelys-cli-man
# and holds it to the rules of the help: no hidden command or option, an
# unavailable command marked, the exit status of the contract, the same
# source twice for the same catalog and date, and no warning from mandoc
# where mandoc is installed. usage: scripts/man-check.sh BUILD_DIR
set -eu
build=$1
work=$(mktemp -d "${TMPDIR:-/tmp}/maelys-cli-man.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
failures=0
fail() {
    printf 'man-check: %s\n' "$1" >&2
    failures=$((failures + 1))
}
sections="NAME SYNOPSIS DESCRIPTION COMMANDS GLOBAL_OPTIONS EXIT_STATUS ENVIRONMENT SEE_ALSO"
render() { # render NAME PROGRAM..., the page holding the headings $sections (_ a space)
    name=$1
    shift
    MAELYS_COMMANDS_PATH=/nonexistent tools/maelys-cli-man --date 2026-01-01 "$@" >"$work/$name.1" ||
        fail "$name: no page"
    MAELYS_COMMANDS_PATH=/nonexistent tools/maelys-cli-man --date 2026-01-01 "$@" >"$work/$name.again" ||
        fail "$name: no page the second time"
    cmp -s "$work/$name.1" "$work/$name.again" || fail "$name: two renderings differ"
    if command -v mandoc >/dev/null 2>&1; then
        lint=$(mandoc -T lint "$work/$name.1" 2>&1 || true)
        [ -z "$lint" ] || fail "$name: mandoc -T lint: $lint"
    fi
    for section in $sections; do
        section=$(printf '%s' "$section" | tr _ ' ')
        grep -q "^\.SH $section\$" "$work/$name.1" || fail "$name: no $section section"
    done
    grep -q '^\.TH .* 1 2026-01-01 ' "$work/$name.1" || fail "$name: no title line with the date given"
}
render maelys-hello "$build/bin/maelys-hello"
render hello-py python3 -B python/examples/hello.py
render catalog-surface "$build/tests/catalog_surface"
render default-surface "$build/tests/default_surface"
render maelys "$build/bin/maelys"
# A hidden command or option is never on the page; an unavailable one is
# there, marked with its reason.
page="$work/catalog-surface.1"
! grep -q '__complete' "$page" || fail "catalog-surface: the hidden __complete is on the page"
! grep -q '^\.SS "hidden"' "$page" || fail "catalog-surface: the hidden command is on the page"
! grep -q 'hidden\\-flag' "$page" || fail "catalog-surface: the hidden option is on the page"
grep -q '^Not available in this build: ' "$page" || fail "catalog-surface: an unavailable command is not marked"
grep -q '^\.B 2$' "$page" || fail "catalog-surface: exit status 2 is missing"
# The product's guidance is prose of its own section; the examples are lines to copy.
grep -q '^\.SH NOTES$' "$work/maelys.1" || fail "maelys: its guidance is not in NOTES"
grep -q '^\.SH EXAMPLES$' "$work/maelys-hello.1" || fail "maelys-hello: no EXAMPLES"
grep -q 'default\\-surface \[options\] runs' "$work/default-surface.1" || fail "default-surface: the default command is not told"
# A page in another language: from the product's tables (x-maelys-translations)
# and the framework's; a text the table leaves out stays in English; the same
# page from C and Python; a program without that language gives its English
# page; --untranslated names what the table lacks.
render default-surface-py python3 -B python/tests/default_surface.py
sections="NOM SYNOPSIS DESCRIPTION COMMANDES OPTIONS_GLOBALES CODE_DE_RETOUR ENVIRONNEMENT VOIR_AUSSI"
render default-surface-fr --lang fr "$build/tests/default_surface"
render default-surface-fr-py --lang fr python3 -B python/tests/default_surface.py
grep -q "^Montrer un fichier ; c'est la commande par d\\\\\[u00E9\]faut" "$work/default-surface-fr.1" || fail "default-surface --lang fr: the purpose is not translated"
grep -q "^Watch the entries on stdout" "$work/default-surface-fr.1" || fail "default-surface --lang fr: an untranslated text is not in English"
# The same catalog gives the same page from C and from Python, byte for
# byte, in English and in French: default_surface declares it word for word
# in both, and the usage Python spells is C's (0.12.1).
cmp -s "$work/default-surface.1" "$work/default-surface-py.1" ||
    fail "default-surface: the C and Python pages differ: $(diff "$work/default-surface.1" "$work/default-surface-py.1" | head -4 | tr '\n' ' ')"
cmp -s "$work/default-surface-fr.1" "$work/default-surface-fr-py.1" ||
    fail "default-surface --lang fr: the C and Python pages differ: $(diff "$work/default-surface-fr.1" "$work/default-surface-fr-py.1" | head -4 | tr '\n' ' ')"
MAELYS_COMMANDS_PATH=/nonexistent tools/maelys-cli-man --date 2026-01-01 --lang fr-CA "$build/bin/maelys-hello" >"$work/hello-fr.1"
cmp -s "$work/maelys-hello.1" "$work/hello-fr.1" || fail "maelys-hello --lang fr-CA: a program without French is not its English page"
missing=$(tools/maelys-cli-man --lang fr --untranslated "$build/tests/default_surface")
printf '%s\n' "$missing" | grep -qx "Watch the entries on stdout." || fail "--untranslated does not name a text the table lacks"
! printf '%s\n' "$missing" | grep -qx "List the entries." || fail "--untranslated names a translated text"
! printf '%s\n' "$missing" | grep -q "^NAME\$\|^Optional\.\$" || fail "--untranslated names a text of the framework"
# The same page for a documentation site (--format mdx): the same blocks, one
# heading per section, a command line in a code block; the same bytes from C
# and Python, in English and in French; read from a catalog file as from the
# program; and no text of a catalog read as JSX, an expression or markup.
mdx() { # mdx NAME ARGS...: the MDX page into $work/NAME.mdx
    name=$1
    shift
    MAELYS_COMMANDS_PATH=/nonexistent tools/maelys-cli-man --format mdx "$@" >"$work/$name.mdx" ||
        fail "$name: no MDX page"
}
mdx default "$build/tests/default_surface"
mdx default-py python3 -B python/tests/default_surface.py
mdx default-fr --lang fr "$build/tests/default_surface"
mdx default-fr-py --lang fr python3 -B python/tests/default_surface.py
cmp -s "$work/default.mdx" "$work/default-py.mdx" || fail "default-surface --format mdx: C and Python differ"
cmp -s "$work/default-fr.mdx" "$work/default-fr-py.mdx" || fail "default-surface --format mdx --lang fr: C and Python differ"
for heading in "# default-surface" "## Synopsis" "## Commands" "## Global options" "## Exit status" "### \`show\`"; do
    grep -qxF "$heading" "$work/default.mdx" || fail "default-surface --format mdx: no line '$heading'"
done
grep -qxF "## Options globales" "$work/default-fr.mdx" || fail "default-surface --format mdx --lang fr: not in French"
grep -q '^\.\(TH\|SH\|TP\)' "$work/default.mdx" && fail "default-surface --format mdx: a roff request in the MDX page"
mdx default-body --body-only "$build/tests/default_surface"
head -n 1 "$work/default-body.mdx" | grep -q '^# ' && fail "default-surface --body-only: the page still has its title"
"$build/tests/default_surface" describe --format json >"$work/default.json"
mdx default-file --catalog "$work/default.json" default-surface
cmp -s "$work/default.mdx" "$work/default-file.mdx" || fail "default-surface --catalog: not the page the program gives"
python3 - "$work/default.json" "$work/trap.json" <<'PY'
import json, sys
envelope = json.load(open(sys.argv[1]))
envelope["data"]["x-maelys-summary"] = "<b>bold</b> {props} *stars* `ticks` [link](x) # & \\"
json.dump(envelope, open(sys.argv[2], "w"))
PY
mdx trap --catalog "$work/trap.json" default-surface
grep -qxF '&lt;b&gt;bold&lt;/b&gt; &#123;props&#125; \*stars\* \`ticks\` \[link\](x) \# &amp; \\' "$work/trap.mdx" ||
    fail "--format mdx: a catalog text is not escaped for MDX: $(sed -n 3p "$work/trap.mdx")"
if [ "$failures" -ne 0 ]; then
    printf '%s man check(s) failed\n' "$failures" >&2
    exit 1
fi
printf 'man-check: ok\n'
