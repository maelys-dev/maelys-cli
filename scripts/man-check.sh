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
# C and Python differ in French only where they differ in English.
diff "$work/default-surface.1" "$work/default-surface-py.1" | grep '^[<>]' >"$work/english.diff" || true
diff "$work/default-surface-fr.1" "$work/default-surface-fr-py.1" | grep '^[<>]' >"$work/french.diff" || true
cmp -s "$work/english.diff" "$work/french.diff" || fail "default-surface --lang fr: C and Python pages differ otherwise than in English"
MAELYS_COMMANDS_PATH=/nonexistent tools/maelys-cli-man --date 2026-01-01 --lang fr-CA "$build/bin/maelys-hello" >"$work/hello-fr.1"
cmp -s "$work/maelys-hello.1" "$work/hello-fr.1" || fail "maelys-hello --lang fr-CA: a program without French is not its English page"
missing=$(tools/maelys-cli-man --lang fr --untranslated "$build/tests/default_surface")
printf '%s\n' "$missing" | grep -qx "Watch the entries on stdout." || fail "--untranslated does not name a text the table lacks"
! printf '%s\n' "$missing" | grep -qx "List the entries." || fail "--untranslated names a translated text"
! printf '%s\n' "$missing" | grep -q "^NAME\$\|^Optional\.\$" || fail "--untranslated names a text of the framework"
if [ "$failures" -ne 0 ]; then
    printf '%s man check(s) failed\n' "$failures" >&2
    exit 1
fi
printf 'man-check: ok\n'
