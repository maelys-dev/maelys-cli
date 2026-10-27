# SPDX-License-Identifier: MPL-2.0
"""Word lists over what maelys-hello and hello.py both declare: command words,
identifiers after help and describe, options, option values in both
spellings, operands. hello-parity-check compares what `__complete` returns
for each between C and Python; completion-check drives each in every shell."""

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
    # Global options select no command: after them only, --version (agent-cli/v2 2.22.0, section 6).
    ["--v"], ["--non-interactive", "--v"], ["--lang", "fr", "--"], ["--format=json", "--color", "never", "-"],
    ["--lang", "--v"], ["--help", "--v"], ["--nope", "--v"], ["--non-interactive", "x", "--"],
]
