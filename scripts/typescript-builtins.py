#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Generate/check language-independent built-in data from the C reference.

The TypeScript runtime never invokes C. This development check reads the C
declarations so the built-in catalog and dynamic shell templates cannot drift.
"""
import argparse
import json
import re
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", default="build/release")
parser.add_argument("--write", action="store_true")
args = parser.parse_args()
build = ROOT / args.build
hello = build / "bin/maelys-hello"

def describe(program):
    return json.loads(subprocess.check_output([str(program), "describe", "--format", "json"]))["data"]

data = describe(hello)
metadata = {key: data[key] for key in ("globalOptions", "invariants", "output")}
metadata["commands"] = data["commands"][:5]
files = {
    ROOT / "typescript/src/builtins.ts":
        "// SPDX-License-Identifier: MPL-2.0\n"
        "// Generated from the pinned C reference by scripts/typescript-builtins.py.\n"
        "export const builtins = " + json.dumps(metadata, ensure_ascii=False, indent=2) + " as const;\n"
}
templates = {}
for shell in ("bash", "zsh", "fish"):
    script = subprocess.check_output([str(hello), "completion", shell], text=True)
    templates[shell] = script.replace("maelys_hello", "__FUNCTION_PROGRAM__").replace("maelys-hello", "__PROGRAM__")
files[ROOT / "typescript/src/completion-templates.ts"] = (
    "// SPDX-License-Identifier: MPL-2.0\n// Generated from the C dynamic shell templates.\n"
    "export const completionTemplates = " + json.dumps(templates, indent=2) + ";\n")
languages = {}
for file in sorted((ROOT / "i18n").glob("*.tsv")):
    def unescape(text):
        return re.sub(r"\\(n|t|\\)", lambda m: {"n": "\n", "t": "\t", "\\": "\\"}[m.group(1)], text)
    table = {}
    for line in file.read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        source, target = map(unescape, line.split("\t"))
        if source in table:
            raise SystemExit("typescript-builtins: duplicate translation")
        table[source] = target
    languages[file.stem] = table
files[ROOT / "typescript/src/translations.ts"] = (
    "// SPDX-License-Identifier: MPL-2.0\n// Generated from i18n/*.tsv by scripts/typescript-builtins.py.\n"
    "export const frameworkTranslations: Record<string, Record<string, string>> = " +
    json.dumps(languages, ensure_ascii=False, indent=2) + ";\n")

for name in ("catalog_surface", "default_surface"):
    executable = build / "tests" / name
    catalog = describe(executable)
    commands = [c for c in catalog["commands"] if c["id"] not in ("help", "version", "describe", "completion", "complete.candidates")]
    files[ROOT / "conformance/fixtures" / (name + ".json")] = json.dumps(commands, ensure_ascii=False, indent=2) + "\n"

failures = []
for file, content in files.items():
    if file.exists() and file.read_text() == content:
        continue
    if args.write:
        file.parent.mkdir(parents=True, exist_ok=True)
        temporary = file.with_suffix(file.suffix + ".tmp")
        temporary.write_text(content)
        temporary.replace(file)
    else:
        failures.append(str(file.relative_to(ROOT)))
if failures:
    print("typescript-builtins: stale: " + ", ".join(failures) +
          "; run python3 scripts/typescript-builtins.py --write", file=sys.stderr)
print("typescript-builtins: " + ("FAILED" if failures else "ok (catalog, completion and full declaration surfaces)"))
sys.exit(bool(failures))
