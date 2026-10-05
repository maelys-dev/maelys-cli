#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Every form the completion has to render, in one program: two commands that
share a word, a hidden and an unavailable command, a delegate, a stream, typed
and variadic operands, a choice, a digest, a repeatable and a hidden option.
`make completion-check` drives its scripts in every installed shell and
compares them with its `__complete`; hello.py declares none of the first five."""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import maelys_cli as cli  # noqa: E402


def reply(invocation: cli.Invocation) -> "tuple[dict, int]":
    return {}, cli.EXIT_OK


def handover(invocation: cli.Invocation) -> int:
    return cli.EXIT_OK


PROGRAM = cli.Program("maelys-surface-py", "Maelys completion surface", "1.2.3", [
    cli.read("pair.one", "pair one", "The first of two commands sharing a word.", reply),
    cli.read("pair.two", "pair two", "The second.", reply,
             operands=[cli.operand("NAME", "A free-text operand.", required=False)]),
    cli.read("absent", "absent", "Not in this build.", reply, unavailable="built without it"),
    cli.read("secret", "secret", "Described, never shown.", reply, hidden=True),
    cli.external("tool", "tool", "A command delegated to a helper.", handover),
    cli.stream("pipe", "pipe", "A stream that owns stdio.", handover,
               operands=[cli.operand("TARGET", "Where to.", kind="choice", choices=["here", "there"])],
               options=[cli.flag("--raw", "Do not frame the stream.")]),
    cli.read("pick", "pick", "Typed operands and every kind of option.", reply,
             operands=[cli.operand("KIND", "A choice.", kind="choice", choices=["alpha", "beta"]),
                       cli.operand("REST", "More of them.", required=False, variadic=True, kind="choice",
                                   choices=["x-ray", "yankee"])],
             options=[cli.option("--mode", "A choice.", cli.argument("MODE", "choice", ["fast", "full"])),
                      cli.option("--digest", "A digest.",
                                 cli.argument("DIGEST", "digest", algorithms=["sha256", "sha512"])),
                      cli.option("--tag", "Repeatable.", cli.argument("TAG"), repeatable=True),
                      cli.option("--file", "A path.", cli.argument("FILE", "path")),
                      cli.flag("--quick", "A flag."),
                      cli.flag("--internal", "Accepted, never shown.", hidden=True)]),
])

# Word lists over those forms. Where `__complete` returns nothing the current
# word is empty or the prefix of the files completion-check creates.
WORD_LISTS = [
    [""], ["p"], ["pa"], ["s"], ["a"], ["pair", ""], ["pair", "o"], ["pair", "t"],
    ["pair", "two", ""], ["pair", "two", "zz-completion-"], ["pair", "two", "--"],
    ["help", ""], ["help", "pa"], ["describe", "p"], ["describe", "a"], ["describe", "s"],
    ["tool", ""], ["tool", "zz-completion-"], ["tool", "run", ""],
    ["pipe", ""], ["pipe", "t"], ["pipe", "--"], ["pipe", "here", "--"], ["pipe", "here", ""],
    ["pick", ""], ["pick", "b"], ["pick", "alpha", ""], ["pick", "alpha", "x-ray", ""], ["pick", "alpha", "x-ray", "y"],
    ["pick", "--"], ["pick", "--q"], ["pick", "--in"], ["pick", "--mode", ""], ["pick", "--mode", "fu"],
    ["pick", "--mode=f"], ["pick", "--mode="], ["pick", "--mode", "fast", ""], ["pick", "--mode", "fast", "--m"],
    ["pick", "--mode=fast", ""], ["pick", "--mode=fast", "--mo"],
    ["pick", "--digest", ""], ["pick", "--digest", "sha5"], ["pick", "--digest", "sha256:ab", ""],
    ["pick", "--tag", "one", "--t"], ["pick", "--tag", "one", ""], ["pick", "--tag", "one", "alpha", ""],
    ["pick", "--file", ""], ["pick", "--file", "zz-completion-"], ["pick", "--quick", ""], ["pick", "--quick", "alpha", ""],
    ["pick", "--", ""], ["pick", "--", "alpha", ""], ["pick", "--", "--"], ["pick", "--internal", ""],
    ["pick", "--format", ""], ["pick", "--format", "json", "--f"], ["pick", "--color", "a"],
]

if __name__ == "__main__":
    sys.exit(PROGRAM.main(sys.argv[1:]))
