#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""The two reference products describe and complete the same declaration alike.

Usage: hello-parity-check.py C_HELLO PYTHON_HELLO

Runs `describe limits` on both, and compares the value members of every
option and operand the two declare under the same name: type, choices,
minimum, maximum, algorithms, digits, pattern. Then compares what
`__complete` returns for the same word lists, in the same order, and the
three completion scripts that call `__complete`, which are one text in both
implementations once the program's name is set aside. Those are the scripts
a C product prints; a Python product prints them when it asks for no static
completion, and scripts that carry its candidates otherwise, which
`make completion-check` drives. The C product is the reference; a member
Python describes otherwise, a word it completes otherwise or a script line
it prints otherwise is a defect of the Python framework or of hello.py,
which exists to mirror maelys-hello. Exit 1 with one line per difference, 0
when none differs.
"""
import importlib.util
import json
import pathlib
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from hello_words import WORD_LISTS  # noqa: E402

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


def catalog(program: list) -> dict:
    out = subprocess.run([*program, "describe", "--summary", "--format", "json"],
                         check=True, capture_output=True, text=True).stdout
    return json.loads(out)["data"]


def candidates(program: list, words: list) -> list:
    return subprocess.run([*program, "__complete", "--", *words],
                          check=True, capture_output=True, text=True).stdout.split()


def neutral(out: str, name: str) -> str:
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
    spec = importlib.util.spec_from_file_location("hello", argv[2])
    hello = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(pathlib.Path(argv[2]).resolve().parent.parent))
    spec.loader.exec_module(hello)
    for shell in ("bash", "zsh", "fish"):
        printed = subprocess.run([*c_hello, "completion", shell], check=True, capture_output=True, text=True).stdout
        expected = neutral(printed, c_catalog["program"])
        offered = neutral(hello.PROGRAM.completion_script(shell, static=False), python_catalog["program"])
        if expected != offered:
            unlike += 1
            lines = [f"C {a!r}, Python {b!r}" for a, b in zip(expected.splitlines(), offered.splitlines()) if a != b]
            print(f"hello-parity-check: completion {shell}: the scripts differ: "
                  f"{lines[0] if lines else 'one is longer'}", file=sys.stderr)
    # The commands both declare carry the same examples.
    def examples(program: list) -> dict:
        out = subprocess.run([*program, "describe", "--format", "json"], check=True, capture_output=True, text=True).stdout
        return {item["id"]: item.get("examples", []) for item in json.loads(out)["data"]["commands"]}
    c_examples, python_examples = examples(c_hello), examples(python_hello)
    for identifier in sorted(shared - {"help", "version", "describe", "completion", "complete.candidates"}):
        if c_examples.get(identifier) != python_examples.get(identifier):
            unlike += 1
            print(f"hello-parity-check: examples of {identifier}: C {json.dumps(c_examples.get(identifier))}, "
                  f"Python {json.dumps(python_examples.get(identifier))}", file=sys.stderr)
    # A plan bound to its application carries the same fingerprint in both:
    # the two products frame the same entries the same way.
    import tempfile
    with tempfile.TemporaryDirectory() as directory:
        existing = pathlib.Path(directory) / "existing.txt"
        existing.write_text("already here", encoding="utf-8")
        for words in (["note", "write", str(pathlib.Path(directory) / "absent.txt"), "--content", "hi"],
                      ["note", "write", str(existing), "--content", "hi", "--replace"]):
            prints = [subprocess.run([*program, *words, "--field", "fingerprint"], check=True, capture_output=True,
                                     text=True).stdout.strip() for program in (c_hello, python_hello)]
            if prints[0] != prints[1] or not prints[0].startswith("sha256:"):
                unlike += 1
                print(f"hello-parity-check: fingerprint of {' '.join(words[:2])} {words[3:]}: C {prints[0]}, "
                      f"Python {prints[1]}", file=sys.stderr)
    if not differing and not unlike:
        print(f"hello-parity-check: ok ({len(reference.keys() & found.keys())} shared members, "
              f"{len(WORD_LISTS)} word lists, 3 scripts, 2 fingerprints)")
    return 1 if differing or unlike else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
