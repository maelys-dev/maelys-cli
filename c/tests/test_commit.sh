#!/bin/sh
# Exercises tools/maelys-cli-commit: a git checkout answers from git; a
# source archive with no .git (what GitHub's tag tarball is, built with
# `git archive`, which expands the export-subst placeholder of ../COMMIT
# before this script runs) answers from that file; neither answers unknown.
set -eu

tool=$1
work=$(mktemp -d "${TMPDIR:-/tmp}/maelys-cli-commit.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

mkdir -p "$work/repo/tools"
cp "$tool" "$work/repo/tools/maelys-cli-commit"

# A git checkout: git wins over any COMMIT file, even a stale one.
git -C "$work/repo" init -q
git -C "$work/repo" -c user.email=t@t -c user.name=t commit -q --allow-empty -m init
expected=$(git -C "$work/repo" rev-parse --short=7 HEAD)
printf 'stale\n' >"$work/repo/COMMIT"
actual=$("$work/repo/tools/maelys-cli-commit")
[ "$actual" = "$expected" ] || { echo "git checkout: got $actual, expected $expected" >&2; exit 1; }

# No .git, COMMIT expanded by `git archive` (export-subst ran): its first 7
# characters answer.
rm -rf "$work/repo/.git"
printf '609a1f776cd8dfaef4cb0e103cd9a10c5f6497ab\n' >"$work/repo/COMMIT"
actual=$("$work/repo/tools/maelys-cli-commit")
[ "$actual" = "609a1f7" ] || { echo "expanded COMMIT: got $actual" >&2; exit 1; }

# No .git, COMMIT still the literal placeholder (a plain download of this
# file, or export-subst not configured): unknown, not the placeholder text.
printf '$Format:%%H$\n' >"$work/repo/COMMIT"
actual=$("$work/repo/tools/maelys-cli-commit")
[ "$actual" = "unknown" ] || { echo "placeholder COMMIT: got $actual" >&2; exit 1; }

# No .git, no COMMIT file at all: unknown.
rm -f "$work/repo/COMMIT"
actual=$("$work/repo/tools/maelys-cli-commit")
[ "$actual" = "unknown" ] || { echo "missing COMMIT: got $actual" >&2; exit 1; }

printf '%s\n' 'test_commit: ok'
