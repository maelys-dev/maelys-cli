#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""A program that declares a default command (spec, section 8, "A line that
names no command"), the catalog of tests/default_surface.c word for word:
tests/test_cli.sh holds the two implementations to the same answers. Nothing
here writes: `save` plans and applies nothing."""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import maelys_cli as cli  # noqa: E402


def show(invocation: cli.Invocation) -> "tuple[dict, int]":
    return {"label": invocation.option("--label") or "", "level": invocation.option("--level") or "",
            "file": invocation.operands[0] if invocation.operands else ""}, cli.EXIT_OK


def listing(invocation: cli.Invocation) -> "tuple[dict, int]":
    limit = invocation.option("--limit")
    return {"limit": "" if limit is None else str(limit)}, cli.EXIT_OK


def save(invocation: cli.Invocation) -> "tuple[dict, int]":
    return {"mode": "apply" if invocation.apply else "plan", "changed": False}, cli.EXIT_OK


PROGRAM = cli.Program("default-surface", "Default surface", "1.0.0", [
    cli.read("show", "show", "Show a file, the default command.", show,
             operands=[cli.operand("FILE", "File to show.", required=False)],
             options=[cli.option("--label", "Label of what is shown.", cli.argument("TEXT")),
                      cli.option("--count", "How many lines.", cli.argument("N", "unsigned", minimum=1, maximum=10)),
                      cli.option("--level", "Level of detail.", cli.argument("LEVEL", "choice", choices=["low", "high"]))]),
    cli.read("list", "list", "List the entries.", listing,
             options=[cli.option("--limit", "Maximum number of entries.",
                                 cli.argument("N", "unsigned", minimum=0, maximum=1000))]),
    cli.transaction("save", "save", "Save a text; plans by default.", save,
                    options=[cli.option("--content", "Text to save.", cli.argument("TEXT"), required=True)],
                    schema={"type": "object", "required": ["mode", "changed"]}),
], guide="a program whose lines may name no command", default_command="show")

if __name__ == "__main__":
    sys.exit(PROGRAM.main(sys.argv[1:]))
