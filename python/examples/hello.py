#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""maelys-hello-py: the reference product of the Python framework.

The same four commands as examples/hello/main.c, on maelys_cli.py: a read
with typed options, a read that echoes every value kind, a transaction that
plans and writes with --apply, and a records command. The conformance kit
of agent-cli-spec runs against it in `make check`.
"""
from __future__ import annotations

import os
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import maelys_cli as cli  # noqa: E402

VERSION = "0.1.0"


def greet(invocation: cli.Invocation):
    name = invocation.operands[0]
    times = invocation.option("--times")
    if invocation.flag("--trace"):
        invocation.program.warn(f"greet: name={name} times={times} shout={invocation.flag('--shout')}")
    invocation.detail(f"greeting {name} {times} time(s)")
    invocation.show_progress(f"greeting {name}")
    invocation.progress_done()
    greeting = f"Hello, {name}!"
    if invocation.flag("--shout"):
        greeting = greeting.upper()
    return {"greeting": greeting, "times": times, "lines": [greeting] * times}, cli.EXIT_OK


def limits(invocation: cli.Invocation):
    data = {"memory": invocation.option("--memory"), "wallTimeMs": invocation.option("--wall-time"),
            "level": invocation.option("--level"), "offset": invocation.option("--offset"),
            "digest": invocation.option("--digest"), "tags": invocation.option("--tag", []),
            "strict": invocation.flag("--strict"), "lenient": invocation.flag("--lenient")}
    return data, cli.EXIT_OK


def note_write(invocation: cli.Invocation):
    path = invocation.operands[0]
    content = invocation.option("--content")
    exists = os.path.lexists(path)
    if exists and not invocation.flag("--replace"):
        raise cli.Failure("PRECONDITION_FAILED", f"{path} already exists.",
                          "Add --replace to replace it, or choose another file.")
    plan = {"mode": "plan", "path": path, "bytes": len(content.encode("utf-8")),
            "action": "replace" if exists else "create"}
    if not invocation.apply:
        return plan, cli.EXIT_OK
    try:
        cli.write_file_atomic(path, content.encode("utf-8"), 0o644,
                              cli.WRITE_REPLACE if invocation.flag("--replace") else cli.WRITE_NO_REPLACE)
    except OSError as error:
        raise cli.file_failure(error, path) from None
    plan["mode"] = "apply"
    return plan, cli.EXIT_OK


def listing(invocation: cli.Invocation):
    limit = invocation.option("--limit")
    samples = [{"name": "alpha", "size": 1}, {"name": "beta", "size": 2}, {"name": "gamma", "size": 3}]
    records = samples[:limit]
    return {"count": len(records), "records": records}, cli.EXIT_OK


def check(invocation: cli.Invocation):
    """A validation: exit 2 reports the violations it found, in data."""
    words = invocation.operands
    violations = [{"word": word, "reason": "not lowercase"} for word in words if word != word.lower()]
    return {"valid": not violations, "violations": violations}, (cli.EXIT_VIOLATIONS if violations else cli.EXIT_OK)


LEVELS = ["quiet", "normal", "verbose"]
PROGRAM = cli.Program("maelys-hello-py", "Maelys Hello (Python)", VERSION, [
    cli.read("greet", "greet", "Greet someone.", greet,
             operands=[cli.operand("NAME", "Person or system to greet.")],
             options=[cli.flag("--shout", "Render the greeting in capitals."),
                      cli.option("--times", "Repeat the greeting.", cli.argument("N", "unsigned", minimum=1, maximum=10),
                                 default="1"),
                      cli.flag("--trace", "Report the parsed input on stderr; diagnostics.", hidden=True)],
             schema={"type": "object", "required": ["greeting", "times", "lines"],
                     "properties": {"greeting": {"type": "string"}, "times": {"type": "integer"},
                                    "lines": {"type": "array", "items": {"type": "string"}}}}),
    cli.read("limits", "limits", "Echo typed option values.", limits,
             options=[cli.option("--memory", "Memory ceiling; K/M/G accepted.", cli.argument("BYTES", "size", minimum=1)),
                      cli.option("--wall-time", "Wall-clock budget with its unit.", cli.argument("DURATION", "duration")),
                      cli.option("--level", "Verbosity level.", cli.argument("LEVEL", "choice", LEVELS), default="normal"),
                      cli.option("--offset", "Signed adjustment.", cli.argument("N", "integer", minimum=-100, maximum=100)),
                      cli.option("--digest", "Expected SHA-256 digest.", cli.argument("HEX", "hex", minimum=64, maximum=64)),
                      cli.option("--tag", "Free label; repeatable.", cli.argument("TEXT", "string"), repeatable=True),
                      cli.flag("--strict", "Refuse defaults.", requires=("--level",)),
                      cli.flag("--lenient", "Accept defaults.", conflicts_with=("--strict",))],
             schema={"type": "object"}),
    cli.transaction("note.write", "note write", "Store a note in a file.", note_write,
                    operands=[cli.operand("FILE", "Destination file; never replaced without --replace.", kind="path")],
                    options=[cli.option("--content", "Text to store.", cli.argument("TEXT", "string"), required=True),
                             cli.flag("--replace", "Allow replacing an existing file.")],
                    schema={"type": "object", "required": ["mode", "path", "bytes", "action"]}),
    cli.records("list", "list", "List sample records.", listing,
                options=[cli.option("--limit", "Maximum number of records.", cli.argument("N", "unsigned", minimum=0, maximum=1000),
                                    default="1000")],
                schema={"type": "object", "required": ["count", "records"]}),
    cli.read("check", "check", "Check that every word is lowercase; exit 2 lists the others.", check,
             operands=[cli.operand("WORD", "Words to check.", required=False, variadic=True)],
             schema={"type": "object", "required": ["valid", "violations"]}),
], guide="reference product of the Python framework")

if __name__ == "__main__":
    sys.exit(PROGRAM.main())
