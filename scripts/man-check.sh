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
render() { # render NAME PROGRAM...
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
    for section in NAME SYNOPSIS DESCRIPTION COMMANDS "GLOBAL OPTIONS" "EXIT STATUS" ENVIRONMENT "SEE ALSO"; do
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
if [ "$failures" -ne 0 ]; then
    printf '%s man check(s) failed\n' "$failures" >&2
    exit 1
fi
printf 'man-check: ok\n'
