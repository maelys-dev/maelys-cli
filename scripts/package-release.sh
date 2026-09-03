#!/bin/sh
# usage: scripts/package-release.sh TARGET   (linux-x86_64 | linux-arm64 | macos-arm64)
# Checks the framework, installs the dispatcher and the SDK into a staging
# tree and leaves maelys-cli-VERSION-TARGET.tar.gz with its .sha256 in dist/.
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
cd "$root"
target=${1:?TARGET}
version=$(sed -n '1p' VERSION)
case $target in linux-x86_64|linux-arm64|macos-arm64) ;; *) echo "unsupported target: $target" >&2; exit 64 ;; esac
case "$(uname -s):$(uname -m):$target" in
    Linux:x86_64:linux-x86_64|Linux:aarch64:linux-arm64|Linux:arm64:linux-arm64|Darwin:arm64:macos-arm64) ;;
    *) echo "target $target does not match this host" >&2; exit 65 ;;
esac
temp_base=${TMPDIR:-/tmp}
temp_base=${temp_base%/}
work=$(mktemp -d "$temp_base/maelys-cli-package.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
make clean check
make install DESTDIR="$work/stage" PREFIX=/usr/local
mkdir -p dist
name="maelys-cli-$version-$target.tar.gz"
tar -czf "dist/$name" -C "$work/stage" .
if command -v sha256sum >/dev/null 2>&1; then (cd dist && sha256sum "$name" >"$name.sha256")
else (cd dist && shasum -a 256 "$name" >"$name.sha256"); fi
printf '%s\n' "dist/$name"
