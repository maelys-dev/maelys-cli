#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""C source directories exist only under c/, never as root aliases."""
from pathlib import Path
import sys
import tempfile

NAMES = ("include", "src", "cli", "tests", "examples")

def check(root):
    failures = []
    for name in NAMES:
        old = root / name
        if old.exists() or old.is_symlink():
            failures.append(name + " must not exist at the root; use c/" + name)
        canonical = root / "c" / name
        if not canonical.is_dir() or canonical.is_symlink():
            failures.append("missing real C directory: c/" + name)
    if not (root / "python/maelys_cli.py").is_file():
        failures.append("the public Python source path must be preserved")
    return failures

def self_test():
    with tempfile.TemporaryDirectory(prefix="maelys-cli-layout-") as temporary:
        root = Path(temporary)
        for name in NAMES:
            (root / "c" / name).mkdir(parents=True)
        (root / "python").mkdir()
        (root / "python/maelys_cli.py").touch()
        assert not check(root)
        for name in NAMES:
            old = root / name
            old.symlink_to("c/" + name)
            assert check(root), "root compatibility link accepted"
            old.unlink()
            old.symlink_to("missing")
            assert check(root), "dangling compatibility link accepted"
            old.unlink()
            old.mkdir()
            assert check(root), "duplicate root tree accepted"
            old.rmdir()
            (root / "c" / name).rmdir()
            assert check(root), "missing canonical directory accepted"
            (root / "c" / name).mkdir()
        assert not check(root)

if __name__ == "__main__":
    self_test()
    failures = check(Path(__file__).resolve().parent.parent)
    for failure in failures:
        print("layout-check: " + failure, file=sys.stderr)
    print("layout-check: " + ("FAILED" if failures else "ok (C only under c/, root aliases refused)"))
    sys.exit(bool(failures))
