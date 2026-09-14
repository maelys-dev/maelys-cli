#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Validates `describe` of a maximal catalog against the pinned specification.

The conformance kit judges the reference products, which declare what a
product plausibly needs. This judges what the *framework* can serialize,
which is a larger set: a macro no product uses is a macro whose output no
schema has ever seen. MAELYS_CLI_HEX_OR was one, emitting an
`alternativeDigits` member that the 2.4 `argument` definition, closed with
`additionalProperties: false`, does not allow.

Two things are checked, and the second is what keeps the first honest:

  1. every describe form of tests/catalog_surface.c validates against
     schemas/describe.json of the pinned agent-cli-spec, using that
     specification's own validator rather than a second implementation;
  2. tests/catalog_surface.c names every declaration macro of
     include/maelys/cli/catalog.h, so a macro added later cannot escape
     the check by simply not being exercised.

Usage: describe-schema-check.py PROGRAM SPEC_DIR
"""
from __future__ import annotations

import json
import pathlib
import re
import subprocess
import sys

# Structural helpers: they carry no declaration of their own, so exercising
# them is what every other macro in the fixture already does.
STRUCTURAL = {
    "MAELYS_CLI_COUNT",
    "MAELYS_CLI_OPERANDS",
    "MAELYS_CLI_OPTIONS",
    "MAELYS_CLI_STRINGIFY",
    "MAELYS_CLI_STRINGIFY_",
    "MAELYS_CLI_CATALOG_PART",
}

FIXTURE = pathlib.Path("tests/catalog_surface.c")
HEADER = pathlib.Path("include/maelys/cli/catalog.h")


def without_comments(source: str) -> str:
    """The code of a C file, its comments removed.

    The guard reads what the fixture *declares*, not what it mentions: this
    file's own header comment names MAELYS_CLI_HEX_OR as the macro that
    motivated the check, and that must not be what makes the macro count as
    exercised.
    """
    return re.sub(r"/\*.*?\*/|//[^\n]*", " ", source, flags=re.DOTALL)


def macros_are_all_exercised() -> list[str]:
    """The macros catalog.h defines and the fixture never writes."""
    # Line continuations joined first: a multi-line macro's body otherwise
    # starts with the backslash, not with the initializer that identifies it.
    header = re.sub(r"\\\n\s*", " ", without_comments(HEADER.read_text()))
    # A declaration macro expands to designated initializers, so its body
    # starts with a dot: that is what tells MAELYS_CLI_APPLY_OPTION, which
    # takes no argument, from the header's include guard and its size
    # constants, which are #defines of the same shape and declare nothing.
    declared = {
        name
        for name, body in re.findall(
            r"^#define (MAELYS_CLI_[A-Z0-9_]+)(?:\([^)]*\))?[ \t]+((?:.*\\\n)*.*)",
            header,
            re.MULTILINE,
        )
        if body.lstrip().startswith((".", "{"))
    }
    used = set(re.findall(r"\bMAELYS_CLI_[A-Z0-9_]+\b", without_comments(FIXTURE.read_text())))
    return sorted(declared - STRUCTURAL - used)


def describe(program: str, *arguments: str) -> dict:
    """The `data` of one describe form, or exit with what went wrong."""
    completed = subprocess.run(
        [program, "describe", *arguments, "--format", "json"],
        capture_output=True,
        text=True,
        check=False,
    )
    if completed.returncode != 0:
        sys.exit(
            f"describe-schema-check: {program} describe {' '.join(arguments)} "
            f"exited {completed.returncode}: {completed.stderr.strip()}"
        )
    return json.loads(completed.stdout)["data"]


def main() -> int:
    if len(sys.argv) != 3:
        sys.exit("usage: describe-schema-check.py PROGRAM SPEC_DIR")
    program, spec_dir = sys.argv[1], pathlib.Path(sys.argv[2])

    conformance = spec_dir / "conformance"
    schema_path = spec_dir / "schemas" / "describe.json"
    if not (conformance / "validate.py").is_file() or not schema_path.is_file():
        sys.exit(
            f"describe-schema-check: {spec_dir} carries no conformance/validate.py "
            "and schemas/describe.json; MAELYS_DEPENDENCIES_DIR must name the root "
            "'maelys-release dependencies . --apply' materialised, or "
            "AGENT_CLI_SPEC_DIR the pinned checkout"
        )
    sys.path.insert(0, str(conformance))
    from validate import validate  # noqa: E402  (the pinned specification's own)

    schema = json.loads(schema_path.read_text())

    missing = macros_are_all_exercised()
    if missing:
        for name in missing:
            print(f"{FIXTURE}: never exercises {name}", file=sys.stderr)
        print(
            f"describe-schema-check: {len(missing)} macro(s) of {HEADER} are not in "
            f"{FIXTURE}; a macro no fixture writes is one no schema has judged",
            file=sys.stderr,
        )
        return 1

    # The catalog-wide form, the summary form, and every command on its own:
    # single-command describe is a different shape, and a member the framework
    # only emits per command would be missed by the catalog form alone.
    forms: list[tuple[str, dict]] = [
        ("describe", describe(program)),
        ("describe --summary", describe(program, "--summary")),
    ]
    for command in forms[0][1]["commands"]:
        identifier = command["id"]
        forms.append((f"describe {identifier}", describe(program, identifier)))

    failed = 0
    for name, data in forms:
        issues = validate(data, schema, schema)
        for issue in issues:
            print(f"{name}: {issue}", file=sys.stderr)
        failed += len(issues)

    if failed:
        print(
            f"describe-schema-check: {failed} issue(s) against {schema_path}",
            file=sys.stderr,
        )
        return 1
    print(f"describe-schema-check: ok ({len(forms)} describe forms)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
