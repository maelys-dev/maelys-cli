# SPDX-License-Identifier: MPL-2.0
"""Contract surface of maelys_cli.py, from the inside: the causal order of
refusals, the value kinds, plan and apply, the envelopes and exit codes.
The conformance kit of agent-cli-spec checks the same program from the
outside in `make check`."""
from __future__ import annotations

import contextlib
import io
import json
import os
import pathlib
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE.parent / "examples"))
import maelys_cli as cli  # noqa: E402
import hello  # noqa: E402


def run(*argv: str, env: dict | None = None) -> tuple[int, str, str]:
    out, err = io.StringIO(), io.StringIO()
    saved = dict(os.environ)
    os.environ.pop("MAELYS_CLI_FORMAT", None)
    os.environ["NO_COLOR"] = "1"
    os.environ.update(env or {})
    try:
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = hello.PROGRAM.main(list(argv))
    finally:
        os.environ.clear()
        os.environ.update(saved)
    return code, out.getvalue(), err.getvalue()


def failure(*argv: str) -> tuple[int, dict]:
    code, out, err = run(*argv, "--json")
    assert out == "", out
    return code, json.loads(err)["error"]


class Contract(unittest.TestCase):
    def test_success_envelope_and_exit_codes(self) -> None:
        code, out, err = run("greet", "World", "--json", "--compact")
        self.assertEqual((code, err), (0, ""))
        body = json.loads(out)
        self.assertEqual(body["contract"], "agent-cli/v2")
        self.assertEqual((body["command"], body["ok"], body["exitCode"]), ("greet", True, 0))
        self.assertEqual(body["data"]["greeting"], "Hello, World!")
        code, out, _ = run("check", "Ab", "cd", "--format", "json")
        self.assertEqual(code, 2)
        self.assertTrue(json.loads(out)["ok"])

    def test_causal_order_of_refusals(self) -> None:
        self.assertEqual(failure("nope")[1]["code"], "INVALID_COMMAND")
        self.assertEqual(failure("greet", "x", "--bogus")[1]["code"], "VALIDATION_FAILED")
        code, error = failure("greet", "x", "--shout", "--shout")
        self.assertIn("twice", error["message"])
        self.assertEqual(failure("greet", "x", "--times", "0")[1]["message"], "Option --times must be between 1 and 10, not 0.")
        self.assertEqual(failure("limits", "--strict")[1]["message"], "Option --strict requires --level.")
        self.assertIn("conflicts with --strict", failure("limits", "--level", "quiet", "--strict", "--lenient")[1]["message"])
        self.assertIn("required", failure("note", "write", "f")[1]["message"])
        self.assertIn("Operands do not match", failure("greet")[1]["message"])
        code, out, err = run("greet", "x", "--format", "jsonl")
        self.assertEqual((code, out), (1, ""))
        self.assertIn("jsonl", err)

    def test_value_kinds(self) -> None:
        code, out, _ = run("limits", "--memory", "4K", "--wall-time", "2m", "--level", "verbose", "--offset", "-5",
                           "--digest", "a" * 64, "--tag", "x", "--tag", "y", "--json")
        data = json.loads(out)["data"]
        self.assertEqual((data["memory"], data["wallTimeMs"], data["level"], data["offset"]), (4096, 120000, "verbose", -5))
        self.assertEqual(data["tags"], ["x", "y"])
        for argv in (("limits", "--memory", "4X"), ("limits", "--wall-time", "5"), ("limits", "--level", "loud"),
                     ("limits", "--offset", "101"), ("limits", "--digest", "zz")):
            self.assertEqual(failure(*argv)[1]["code"], "VALIDATION_FAILED", argv)

    def test_defaults_come_from_the_catalog(self) -> None:
        code, out, _ = run("greet", "x", "--json")
        self.assertEqual(json.loads(out)["data"]["times"], 1)
        code, out, _ = run("limits", "--json")
        self.assertEqual(json.loads(out)["data"]["level"], "normal")

    def test_transaction_plans_then_applies(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "note.txt")
            code, out, _ = run("note", "write", path, "--content", "hi", "--json")
            self.assertEqual((code, json.loads(out)["data"]["mode"]), (0, "plan"))
            self.assertFalse(os.path.exists(path))
            code, out, _ = run("note", "write", path, "--content", "hi", "--apply", "--json")
            self.assertEqual((code, json.loads(out)["data"]["mode"]), (0, "apply"))
            self.assertEqual(pathlib.Path(path).read_text(), "hi")
            code, error = failure("note", "write", path, "--content", "again")
            self.assertEqual(error["code"], "PRECONDITION_FAILED")
            code, error = failure("note", "write", path, "--content", "x", "--dry-run")
            self.assertEqual(error["code"], "VALIDATION_FAILED")
            self.assertIn("--apply", error["hint"])

    def test_records_render_as_jsonl_and_text(self) -> None:
        code, out, _ = run("list", "--limit", "2", "--format", "jsonl")
        self.assertEqual([json.loads(line)["name"] for line in out.splitlines()], ["alpha", "beta"])
        code, out, _ = run("list", "--limit", "1")
        self.assertEqual(out, '{"name":"alpha","size":1}\n')

    def test_describe_forms(self) -> None:
        code, out, _ = run("describe", "--json")
        catalog = json.loads(out)["data"]
        self.assertEqual(catalog["kind"], "catalog")
        self.assertIn("globalOptions", catalog)
        ids = [c["id"] for c in catalog["commands"]]
        self.assertEqual(ids[:5], ["help", "version", "describe", "completion", "complete.candidates"])
        code, out, _ = run("describe", "--summary", "--prefix", "note", "--json")
        summary = json.loads(out)["data"]
        self.assertEqual(summary["filter"], {"kind": "command-prefix", "value": "note"})
        self.assertEqual([c["id"] for c in summary["commands"]], ["note.write"])
        self.assertNotIn("outputSchema", summary["commands"][0])
        self.assertEqual(failure("describe", "--prefix", "note")[1]["code"], "VALIDATION_FAILED")
        self.assertEqual(failure("describe", "greet", "--summary", "--prefix", "note")[1]["code"], "VALIDATION_FAILED")
        self.assertEqual(failure("describe", "--summary", "--prefix", "zzz")[1]["code"], "INVALID_COMMAND")
        self.assertEqual(failure("describe", "nope")[1]["code"], "INVALID_COMMAND")
        code, out, _ = run("describe", "note.write", "--json")
        descriptor = json.loads(out)["data"]["commands"][0]
        self.assertEqual(descriptor["effect"], {"plan": "preview", "apply": "apply"})
        self.assertEqual(descriptor["usage"], descriptor["input"]["synopsis"])
        self.assertIn("--apply", [o["long"] for o in descriptor["input"]["options"]])

    def test_help_and_version(self) -> None:
        code, out, _ = run("--help")
        self.assertIn("COMMANDS", out)
        code, out, _ = run("greet", "--help")
        self.assertTrue(out.startswith("maelys-hello-py greet NAME"))
        code, out, _ = run("--version", "--json")
        self.assertEqual(json.loads(out)["data"]["version"], hello.VERSION)

    def test_completion(self) -> None:
        code, out, _ = run("__complete", "--json", "--", "no")
        self.assertEqual([r["word"] for r in json.loads(out)["data"]["records"]], ["note"])
        code, out, _ = run("__complete", "--", "note", "write", "f", "--")
        self.assertIn("--content", out)
        self.assertNotIn("__complete", out)
        code, out, _ = run("completion", "bash")
        self.assertIn("__complete", out)
        self.assertEqual(failure("completion", "ksh")[1]["code"], "VALIDATION_FAILED")

    def test_text_failure_and_format_environment(self) -> None:
        code, out, err = run("greet")
        self.assertEqual(code, 1)
        self.assertTrue(err.startswith("maelys-hello-py: [VALIDATION_FAILED]"))
        self.assertIn("Hint:", err)
        code, out, err = run("greet", env={"MAELYS_CLI_FORMAT": "json"})
        self.assertEqual(json.loads(err)["error"]["code"], "VALIDATION_FAILED")

    def test_catalog_refuses_bad_declarations(self) -> None:
        with self.assertRaises(ValueError):
            cli.read("Bad", "bad", "x", lambda i: ({}, 0))
        with self.assertRaises(ValueError):
            cli.Program("p", "P", "1", [cli.read("a", "a", "x", lambda i: ({}, 0),
                                              options=[cli.flag("--x", "x", requires=("--y",))])])
        with self.assertRaises(ValueError):
            cli.Program("p", "P", "1", [cli.read("help", "h", "x", lambda i: ({}, 0))])


if __name__ == "__main__":
    unittest.main()
