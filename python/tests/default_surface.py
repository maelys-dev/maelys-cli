#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""A program that declares a default command (spec, section 8, "A line that
names no command"), the catalog of tests/default_surface.c word for word:
tests/test_cli.sh holds the two implementations to the same answers. Nothing
here writes: `save` plans and applies nothing.

Two variables serve the migration tests, as in C. DEFAULT_SURFACE_DEFAULT
names the default command (`show` when unset, `none` for no default), so that
the same catalog answers before and after a default is declared.
DEFAULT_SURFACE_TRACE names a file every handler appends its command to, the
one thing a handler writes: a refused line leaves it empty."""
import os
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import maelys_cli as cli  # noqa: E402


def trace(command: str) -> None:
    path = os.environ.get("DEFAULT_SURFACE_TRACE")
    if path:
        with open(path, "a", encoding="utf-8") as file:
            file.write(command + "\n")


def show(invocation: cli.Invocation) -> "tuple[dict, int]":
    trace("show")
    label = invocation.option("--label") or ""
    if label.startswith("!"):
        # A failure of the product's own, whose template its table translates.
        raise cli.failure("PRECONDITION_FAILED", "Label %s is reserved.", label,
                          hint="Choose a label that does not start with '!'.")
    return {"label": invocation.option("--label") or "", "level": invocation.option("--level") or "",
            "file": invocation.operands[0] if invocation.operands else ""}, cli.EXIT_OK


def listing(invocation: cli.Invocation) -> "tuple[dict, int]":
    trace("list")
    limit = invocation.option("--limit")
    return {"limit": "" if limit is None else str(limit)}, cli.EXIT_OK


def save(invocation: cli.Invocation) -> "tuple[dict, int]":
    trace("save")
    return {"mode": "apply" if invocation.apply else "plan", "changed": False}, cli.EXIT_OK


def watch(invocation: cli.Invocation) -> int:
    trace("watch")
    sys.stdout.write("watching\n")
    sys.stdout.flush()
    return cli.EXIT_OK


# French for part of the catalog, the table of tests/default_surface.c: the texts left out stay in English.
FRENCH = {
    'a program whose lines may name no command':
        'un programme dont les lignes peuvent ne nommer aucune commande',
    'Show a file, the default command.':
        "Montrer un fichier ; c'est la commande par défaut.",
    'List the entries.':
        'Lister les entrées.',
    'Save a text; plans by default.':
        'Enregistrer un texte ; planifie par défaut.',
    'File to show.':
        'Fichier à montrer.',
    'Label of what is shown.':
        'Étiquette de ce qui est montré.',
    'How many lines.':
        'Combien de lignes.',
    'Maximum number of entries.':
        "Nombre maximal d'entrées.",
    'Text to save.':
        'Texte à enregistrer.',
    'Label %s is reserved.':
        "L'étiquette %s est réservée.",
    "Choose a label that does not start with '!'.":
        "Choisissez une étiquette qui ne commence pas par '!'.",
}

NAMED = os.environ.get("DEFAULT_SURFACE_DEFAULT") or "show"

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
    cli.stream("watch", "watch", "Watch the entries on stdout.", watch),
], guide="a program whose lines may name no command", default_command=None if NAMED == "none" else NAMED,
    help_palette={"heading": "1;34", "command": "36", "option": "33", "placeholder": "4", "example": "32"},
    translations={"fr": FRENCH})

if __name__ == "__main__":
    sys.exit(PROGRAM.main(sys.argv[1:]))
