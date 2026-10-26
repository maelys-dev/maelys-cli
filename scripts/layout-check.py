#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compatibility paths select exactly the one canonical C implementation."""
from pathlib import Path
import os
import sys

root = Path(__file__).resolve().parent.parent
failures = []
for name in ("include", "src", "cli", "tests", "examples"):
    alias = root / name
    if not alias.is_symlink() or os.readlink(alias) != "c/" + name:
        failures.append(name + " must be the relative compatibility link c/" + name)
    if not (root / "c" / name).is_dir():
        failures.append("missing canonical C directory: c/" + name)
if not (root / "python/maelys_cli.py").is_file():
    failures.append("the public Python source path must be preserved")
for failure in failures:
    print("layout-check: " + failure, file=sys.stderr)
print("layout-check: " + ("FAILED" if failures else "ok (one C tree, five compatible source paths)"))
sys.exit(bool(failures))
