#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""What a refusal names (agent-cli/v2 2.23.0, section 8): each line below is
refused, and its envelope carries in error.issues exactly one entry, the code
and the path given here, its message being error.message.

The kit of agent-cli-spec proves the codes it can reach through the
built-ins; this script proves the others on the `read` command of the
catalog surface, which declares every value kind, a group, the three
constraint kinds, a required option and typed operands: VALUE_RANGE,
REQUIRED_OPTION, ALL_OR_NONE, AT_MOST_ONE, EXACTLY_ONE, REQUIRES through all
three declarations, CONFLICTS, and the value codes on operands.

Usage: refusal-issues-check.py PROGRAM [ARGS...]
  PROGRAM is c/tests/catalog_surface or `node typescript/tests/surface.mjs`.
"""
from __future__ import annotations

import json
import subprocess
import sys

BASE = ["read", "n", "normal", "/tmp", "--mandatory", "x"]
CASES = [
    # (words after BASE, or a whole line when it starts with "!"; code; path)
    (["--plain", "--count", "0"], "VALUE_RANGE", "--count"),
    (["--plain", "--offset", "101"], "VALUE_RANGE", "--offset"),
    (["--plain", "--memory", "0"], "VALUE_RANGE", "--memory"),
    (["--plain", "--offset", "x"], "VALUE_KIND", "--offset"),
    (["--plain", "--count", "99999999999999999999"], "VALUE_KIND", "--count"),
    (["--plain", "--memory", "1Q"], "VALUE_KIND", "--memory"),
    (["--plain", "--root", "relative"], "VALUE_KIND", "--root"),
    (["--plain", "--short-digest", "ab"], "VALUE_KIND", "--short-digest"),
    (["--plain", "--checksum", "md5:00"], "VALUE_KIND", "--checksum"),
    (["--plain", "--named", "Upper"], "VALUE_PATTERN", "--named"),
    (["--plain", "--level", "huge"], "VALUE_CHOICE", "--level"),
    (["--plain", "--plain"], "DUPLICATE_OPTION", "--plain"),
    (["--plain", "--Nope=1"], "UNKNOWN_OPTION", "--Nope"),
    (["--plain", "--named"], "MISSING_VALUE", "--named"),
    (["--plain", "-n"], "NOT_AN_OPTION", "-n"),
    ([], "EXACTLY_ONE", "--plain,--text,--file"),
    (["--plain", "--text", "a"], "EXACTLY_ONE", "--plain,--text,--file"),
    (["--plain", "--strict", "--level", "loud", "--combined", "--paired-a", "--paired-b"],
     "AT_MOST_ONE", "--strict,--lenient,--combined"),
    (["--plain", "--paired-a"], "ALL_OR_NONE", "--paired-a,--paired-b"),
    (["--plain", "--strict"], "REQUIRES", "--strict"),
    (["--plain", "--combined"], "REQUIRES", "--combined"),
    (["--plain", "--named", "a"], "REQUIRES", "--named"),
    (["--plain", "--lenient", "--strict", "--level", "loud"], "CONFLICTS", "--lenient"),
    (["!", "read", "n", "normal", "/tmp", "--plain"], "REQUIRED_OPTION", "--mandatory"),
    (["!", "read", "n", "huge", "/tmp", "--mandatory", "x", "--plain"], "VALUE_CHOICE", "LEVEL"),
    (["!", "read", "n", "normal", "relative", "--mandatory", "x", "--plain"], "VALUE_KIND", "PATH"),
    (["!", "read", "n", "normal", "/tmp", "x", "zz", "--mandatory", "x", "--plain"], "VALUE_KIND", "SUM"),
    (["!", "read", "n", "--mandatory", "x", "--plain"], "MISSING_OPERAND", "LEVEL"),
    (["!", "records", "--format", "jsonl", "--format", "json"], "DUPLICATE_OPTION", "--format"),
    (["!", "records", "--field", "x", "--format", "json"], "RENDERING", "--field"),
    (["!", "nope"], "UNKNOWN_COMMAND", "nope"),
]


def main() -> int:
    program = sys.argv[1:]
    if not program:
        sys.stderr.write(__doc__)
        return 2
    failures = 0
    for words, code, path in CASES:
        line = words[1:] if words[:1] == ["!"] else BASE + words
        done = subprocess.run(program + line, capture_output=True, text=True,
                              env={"PATH": "/usr/bin:/bin:/usr/local/bin:/opt/homebrew/bin",
                                   "MAELYS_CLI_FORMAT": "json", "LC_ALL": "C.UTF-8"})
        try:
            error = json.loads(done.stderr)["error"]
        except (ValueError, KeyError, TypeError):
            print(f"refusal-issues-check: {' '.join(line)!r}: no failure envelope (exit {done.returncode})")
            failures += 1
            continue
        issues = error.get("issues")
        expected = [{"code": code, "message": error.get("message"), "path": path}]
        if done.returncode != 1 or issues != expected:
            print(f"refusal-issues-check: {' '.join(line)!r}: issues {issues}, expected {code} on {path!r}")
            failures += 1
    if failures:
        return 1
    print(f"refusal-issues-check: ok ({len(CASES)} refusals name their cause)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
