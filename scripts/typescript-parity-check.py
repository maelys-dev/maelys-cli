#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""The C, Python and Node runtimes answer the same common declarations."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", default="build/release")
args = parser.parse_args()
build = ROOT / args.build
sys.path.insert(0, str(ROOT / "scripts"))
from hello_words import WORD_LISTS  # noqa: E402

env = dict(os.environ, MAELYS_CLI_FORMAT="json", LC_ALL="C.UTF-8", PYTHONDONTWRITEBYTECODE="1")
env.pop("MAELYS_CLI_LANG", None)
def run(program, words):
    result = subprocess.run([*map(str, program), *words], cwd=ROOT, env=env, capture_output=True, text=True, timeout=20)
    output = result.stdout if result.returncode in (0, 2) else result.stderr
    return result.returncode, json.loads(output)

hello = [[build / "bin/maelys-hello"], [sys.executable, "-B", ROOT / "python/examples/hello.py"],
         ["node", ROOT / "typescript/examples/hello.mjs"]]
cases = [
    ["greet", "Ada"], ["greet", "Ada", "--shout", "--times", "2"],
    ["greet", "Ada", "--times", "0"], ["greet", "--help"], ["greet", "Ada", "--times", "bad", "--help"],
    ["unknown", "--unknown"],
    ["greet", "Ada", "--limit", "1"], ["greet", "Ada", "--shout", "--shout"],
    ["limits", "--memory", "2G", "--wall-time", "3s", "--offset=-2", "--level", "high", "--tag", "a", "--tag", "b"],
    ["limits", "--strict", "--lenient"], ["limits", "--memory", "4m"], ["limits", "--wall-time", "5S"],
]
def normalize(reply):
    status, result = reply
    if not result["ok"]:
        return status, result["command"], result["error"]["code"]
    data = result["data"]
    if result["command"] == "help": data = data["commands"]
    return status, result["command"], data

count = 0
for words in cases:
    replies = [normalize(run(program, words)) for program in hello]
    # Existing sample products intentionally differ: Python's greet adds lines,
    # and limits adds digest:null and the two mode flags. Compare every C field,
    # while requiring Python and Node to carry identical additional fields.
    if replies[0][0] == 0 and replies[0][1] in ("greet", "limits"):
        if replies[1][2] != replies[2][2]:
            raise SystemExit("typescript-parity: Node differs from the Python sample data")
        keys = set(replies[0][2])
        replies = [(status, command, {k: v for k, v in data.items() if k in keys}) for status, command, data in replies]
    if replies[1:] != [replies[0]] * 2:
        raise SystemExit("typescript-parity: differing reply for " + repr(words) + ": " + repr(replies))
    count += 1

# Compare the shared option value vocabulary without conflating product prose.
members = ("name", "type", "choices", "minimum", "maximum", "algorithms", "digits", "pattern")
descriptors = []
for program in hello:
    descriptor = run(program, ["describe", "limits"])[1]["data"]["commands"][0]
    descriptors.append({item["long"]: {k: v for k, v in item.get("argument", {}).items() if k in members}
                        for item in descriptor["input"]["options"]})
if descriptors[1:] != [descriptors[0]] * 2:
    raise SystemExit("typescript-parity: limits declaration differs: " + repr(descriptors))

# Different products can have extra commands. Compare the C/Python oracle with
# Node on word lists whose selected prefix is common, keeping candidate order.
for words in WORD_LISTS:
    answers = [run(program, ["__complete", "--", *words])[1]["data"]["records"] for program in hello]
    common_ids = set(x["word"] for x in answers[1])
    # For root/help discovery C declares extra sample commands; their words
    # are outside the shared reference product, as in hello-parity-check.py.
    if words[0] in ("", "he", "help", "describe"):
        answers[0] = [x for x in answers[0] if x["word"] in common_ids]
    if answers[2] != answers[1]:
        raise SystemExit("typescript-parity: completion differs for " + repr(words) + ": " + repr(answers[1:]))
    count += 1

with tempfile.TemporaryDirectory(prefix="maelys-three-runtime-") as temp:
    target = str(Path(temp) / "note")
    plan = ["note", "write", target, "--content", "same plan"]
    fingerprints = [run(p, plan)[1]["data"]["fingerprint"] for p in hello]
    if len(set(fingerprints)) != 1: raise SystemExit("typescript-parity: absent-file fingerprints differ")
    Path(target).write_text("previous content")
    fingerprints = [run(p, plan + ["--replace"])[1]["data"]["fingerprint"] for p in hello]
    if len(set(fingerprints)) != 1: raise SystemExit("typescript-parity: existing-file fingerprints differ")

defaults = [[build / "tests/default_surface"], [sys.executable, "-B", ROOT / "python/tests/default_surface.py"],
            ["node", ROOT / "typescript/tests/surface.mjs", "--default-fixture"]]
for words in [[], ["--label", "x"], ["--", "file"], ["--help"], ["--label", "x", "--help"],
              ["--label", "x", "file"], ["--count", "0"], ["show", "file"], ["list", "--limit", "3"],
              ["save", "--content", "x"], ["save", "--content", "x", "--apply"], ["missing"]]:
    replies = [normalize(run(program, words)) for program in defaults]
    if replies[1:] != [replies[0]] * 2: raise SystemExit("typescript-parity: default differs for " + repr(words) + ": " + repr(replies))
    count += 1
print("typescript-parity: ok (" + str(count) + " three-runtime cases, typed declarations, absent/existing-file fingerprints)")
