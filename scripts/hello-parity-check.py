#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""The two reference products describe the same declaration in the same shape.

Usage: hello-parity-check.py C_HELLO PYTHON_HELLO

Runs `describe limits` on both, and compares the value members of every
option and operand the two declare under the same name: type, choices,
minimum, maximum, algorithms, digits, pattern. The C product is the
reference; a member Python describes otherwise is a defect of the Python
framework or of hello.py, which exists to mirror maelys-hello. Exit 1 with
one line per differing member, 0 when none differs.
"""
import json
import subprocess
import sys

MEMBERS = ("type", "choices", "minimum", "maximum", "algorithms", "digits", "pattern")


def values(program: list) -> dict:
    out = subprocess.run([*program, "describe", "limits", "--format", "json"],
                         check=True, capture_output=True, text=True).stdout
    data = json.loads(out)["data"]["commands"][0]["input"]
    members = {}
    for item in data["options"]:
        if "argument" in item:
            members[item["long"]] = {k: v for k, v in item["argument"].items() if k in MEMBERS}
    for item in data["operands"]:
        members[item["name"]] = {k: v for k, v in item.items() if k in MEMBERS}
    return members


def main(argv: list) -> int:
    if len(argv) != 3:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    reference, found = values([argv[1]]), values([sys.executable, argv[2]])
    differing = sorted(name for name in reference.keys() & found.keys() if reference[name] != found[name])
    for name in differing:
        print(f"hello-parity-check: {name}: C describes {json.dumps(reference[name])}, "
              f"Python {json.dumps(found[name])}", file=sys.stderr)
    if not differing:
        print(f"hello-parity-check: ok ({len(reference.keys() & found.keys())} shared members)")
    return 1 if differing else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
