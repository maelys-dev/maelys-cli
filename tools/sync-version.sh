#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Regenerates the version macros of include/maelys/cli/version.h from VERSION.
# The version is materialised twice, in VERSION and in the public header;
# `make check-version` fails when they drift, and this script is what closes
# the gap. `maelys-release cut` runs it between writing VERSION and the bump
# commit (maelys-release.conf, [cut] after-version), so the header joins that
# commit and the checks of that commit see a consistent pair.
# Usage: sync-version.sh [--check]; --check only reports.
set -eu
header=include/maelys/cli/version.h
version=$(sed -n '1p' VERSION)
case "$version" in
    [0-9]*.[0-9]*.[0-9]*) ;;
    *) echo "VERSION must be MAJOR.MINOR.PATCH, got '$version'" >&2; exit 1 ;;
esac
major=${version%%.*}
rest=${version#*.}
minor=${rest%%.*}
patch=${rest#*.}
case "$major$minor$patch" in
    *[!0-9]*) echo "VERSION components must be integers, got '$version'" >&2; exit 1 ;;
esac
rendered=$(sed \
    -e "s/^#define MAELYS_CLI_VERSION \".*\"/#define MAELYS_CLI_VERSION \"$version\"/" \
    -e "s/^#define MAELYS_CLI_VERSION_MAJOR .*/#define MAELYS_CLI_VERSION_MAJOR $major/" \
    -e "s/^#define MAELYS_CLI_VERSION_MINOR .*/#define MAELYS_CLI_VERSION_MINOR $minor/" \
    -e "s/^#define MAELYS_CLI_VERSION_PATCH .*/#define MAELYS_CLI_VERSION_PATCH $patch/" \
    "$header")
for macro in "MAELYS_CLI_VERSION " "MAELYS_CLI_VERSION_MAJOR " \
        "MAELYS_CLI_VERSION_MINOR " "MAELYS_CLI_VERSION_PATCH "; do
    printf '%s\n' "$rendered" | grep -q "^#define $macro" || {
        echo "$header: no $macro line to rewrite" >&2
        exit 1
    }
done
if test "$rendered" = "$(cat "$header")"; then
    echo "sync-version: $header already at $version"
    exit 0
fi
if test "${1:-}" = "--check"; then
    echo "sync-version: $header does not match VERSION $version" >&2
    exit 1
fi
printf '%s\n' "$rendered" > "$header"
echo "sync-version: $header now at $version"
