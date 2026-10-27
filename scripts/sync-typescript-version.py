#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Keep the npm package and lock version aligned with the single VERSION."""
import argparse
import json
from pathlib import Path
import re
import sys
import uuid

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--check", action="store_true")
args = parser.parse_args()
version = (root / "VERSION").read_text().strip()
if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
    raise SystemExit("sync-typescript-version: invalid VERSION")
changed = []
for name in ("package.json", "package-lock.json"):
    file = root / "typescript" / name
    document = json.loads(file.read_text())
    before = json.dumps(document, sort_keys=True)
    document["version"] = version
    if name == "package-lock.json":
        if document.get("packages", {}).get("", {}).get("name") != "@maelys-dev/cli":
            raise SystemExit("sync-typescript-version: invalid lock package identity")
        document["packages"][""]["version"] = version
    if json.dumps(document, sort_keys=True) == before:
        continue
    changed.append(name)
    if not args.check:
        temporary = file.with_name(file.name + "." + uuid.uuid4().hex + ".tmp")
        temporary.write_text(json.dumps(document, indent=2) + "\n")
        temporary.replace(file)
if changed and args.check:
    raise SystemExit("sync-typescript-version: stale " + ", ".join(changed) + "; run sh tools/sync-version.sh")
print("sync-typescript-version: ok (" + version + ")")
