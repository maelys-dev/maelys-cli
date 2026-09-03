#!/bin/sh
# Clones maelys-json at the tag and commit recorded in adapter/MAELYS_JSON_PIN.
set -eu
destination=${1:-../maelys-json}
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
tag=$(sed -n '1p' "$root/adapter/MAELYS_JSON_PIN")
pin=$(sed -n '2p' "$root/adapter/MAELYS_JSON_PIN")
if [ -e "$destination" ]; then
    echo "refusing to replace existing path: $destination" >&2
    exit 1
fi
git clone --filter=blob:none --no-checkout https://github.com/maelys-dev/maelys-json.git "$destination"
git -C "$destination" checkout --detach "$tag"
test "$(git -C "$destination" rev-parse HEAD)" = "$pin"
