#!/usr/bin/env python3
"""Holds the failure envelope of PROGRAM WORDS... to the pinned specification's
schemas/envelope.json, in both JSON layouts: extension members of `error`
(maelys_cli_fail_error_extended) must be what the schema admits.

usage: envelope-schema-check.py SPEC_DIR PROGRAM WORDS...
"""
import json
import pathlib
import subprocess
import sys


def main() -> int:
    if len(sys.argv) < 4:
        sys.exit("usage: envelope-schema-check.py SPEC_DIR PROGRAM WORDS...")
    spec_dir, argv = pathlib.Path(sys.argv[1]), sys.argv[2:]
    sys.path.insert(0, str(spec_dir / "conformance"))
    from validate import validate  # noqa: E402  (the pinned specification's own)
    schema = json.loads((spec_dir / "schemas" / "envelope.json").read_text())
    failed = 0
    for layout in ("--compact", "--pretty"):
        done = subprocess.run([*argv, "--json", layout], capture_output=True, text=True, check=False)
        envelope = json.loads(done.stderr)
        error = envelope.get("error", {})
        problems = list(validate(envelope, schema, schema))
        if done.returncode != 1 or done.stdout or not any(name.startswith("x-") for name in error):
            problems.append(f"exit {done.returncode}, stdout {done.stdout[:40]!r}, error {sorted(error)}")
        for problem in problems:
            print(f"{' '.join(argv)} {layout}: {problem}", file=sys.stderr)
        failed += len(problems)
    if failed:
        return 1
    print("envelope-schema-check: ok (an error with extension members, both layouts)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
