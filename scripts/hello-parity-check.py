#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""The two reference products describe and complete the same declaration alike.

Usage: hello-parity-check.py C_HELLO PYTHON_HELLO

Runs `describe limits` on both, and compares the value members of every
option and operand the two declare under the same name: type, choices,
minimum, maximum, algorithms, digits, pattern. Then compares what
`__complete` returns for the same word lists, in the same order, and the
three completion scripts, which are one text in both implementations once
the program's name is set aside. The C product is the reference; a member
Python describes otherwise, a word it completes otherwise or a script line
it prints otherwise is a defect of the Python framework or of hello.py,
which exists to mirror maelys-hello. Exit 1 with one line per difference, 0
when none differs.
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


# Word lists over what both hellos declare: command words, identifiers after
# help and describe, options, option values in both spellings, operands.
WORD_LISTS = [
    [""], ["gr"], ["he"], ["-"], ["--"], ["nope", ""],
    ["help", ""], ["help", "--"], ["help", "note", ""], ["describe", ""], ["describe", "n"], ["describe", "greet", ""],
    ["completion", ""], ["completion", "b"], ["completion", "bash", ""], ["version", ""], ["version", "--"],
    ["greet", ""], ["greet", "-"], ["greet", "--"], ["greet", "--sh"], ["greet", "x", "--"],
    ["greet", "--shout", ""], ["greet", "--shout", "--"], ["greet", "--format", ""], ["greet", "--format=j"],
    ["greet", "--color", ""], ["greet", "--progress", ""], ["greet", "--pager", ""], ["greet", "--field", ""],
    ["limits", ""], ["limits", "--"], ["limits", "abc", ""], ["limits", "--", "--"],
    ["limits", "--level", ""], ["limits", "--level", "--"], ["limits", "--level=l"], ["limits", "--level="],
    ["limits", "--level", "low", "--"], ["limits", "--digest", ""], ["limits", "--digest", "sha"],
    ["limits", "--tag", "a", "--"], ["limits", "--tag", "a", "--tag", "b", "--t"],
    ["note", ""], ["note", "w"], ["note", "write", ""], ["note", "write", "--"],
    ["list", ""], ["list", "--"], ["check", ""], ["check", "--"], ["__complete", ""],
]


def catalog(program: list) -> dict:
    out = subprocess.run([*program, "describe", "--summary", "--format", "json"],
                         check=True, capture_output=True, text=True).stdout
    return json.loads(out)["data"]


def candidates(program: list, words: list) -> list:
    return subprocess.run([*program, "__complete", "--", *words],
                          check=True, capture_output=True, text=True).stdout.split()


def script(program: list, name: str, shell: str) -> str:
    out = subprocess.run([*program, "completion", shell], check=True, capture_output=True, text=True).stdout
    identifier = "".join(c if c.isascii() and c.isalnum() else "_" for c in name)
    return out.replace(name, "PROGRAM").replace(identifier, "PROGRAM")


def main(argv: list) -> int:
    if len(argv) != 3:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    c_hello, python_hello = [argv[1]], [sys.executable, argv[2]]
    reference, found = values(c_hello), values(python_hello)
    differing = sorted(name for name in reference.keys() & found.keys() if reference[name] != found[name])
    for name in differing:
        print(f"hello-parity-check: {name}: C describes {json.dumps(reference[name])}, "
              f"Python {json.dumps(found[name])}", file=sys.stderr)
    # maelys-hello declares commands hello.py does not: their identifiers and
    # pattern words are set aside, everything else must be equal.
    c_catalog, python_catalog = catalog(c_hello), catalog(python_hello)
    shared = {item["id"] for item in python_catalog["commands"]}
    apart = set()
    for item in c_catalog["commands"]:
        if item["id"] not in shared:
            apart.update([item["id"], *item["pattern"]])
    apart -= {word for item in python_catalog["commands"] for word in [item["id"], *item["pattern"]]}
    unlike = 0
    for words in WORD_LISTS:
        expected = [word for word in candidates(c_hello, words) if word not in apart]
        offered = candidates(python_hello, words)
        if expected != offered:
            unlike += 1
            print(f"hello-parity-check: __complete -- {' '.join(words)!r}: C returns {expected}, "
                  f"Python {offered}", file=sys.stderr)
    for shell in ("bash", "zsh", "fish"):
        expected = script(c_hello, c_catalog["program"], shell)
        offered = script(python_hello, python_catalog["program"], shell)
        if expected != offered:
            unlike += 1
            lines = [f"C {a!r}, Python {b!r}" for a, b in zip(expected.splitlines(), offered.splitlines()) if a != b]
            print(f"hello-parity-check: completion {shell}: the scripts differ: "
                  f"{lines[0] if lines else 'one is longer'}", file=sys.stderr)
    if not differing and not unlike:
        print(f"hello-parity-check: ok ({len(reference.keys() & found.keys())} shared members, "
              f"{len(WORD_LISTS)} word lists, 3 scripts)")
    return 1 if differing or unlike else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
