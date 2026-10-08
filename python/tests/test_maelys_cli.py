# SPDX-License-Identifier: MPL-2.0
"""Contract surface of maelys_cli.py, from the inside: the causal order of
refusals, the value kinds, plan and apply, the envelopes and exit codes.
The conformance kit of agent-cli-spec checks the same program from the
outside in `make check`."""
from __future__ import annotations

import contextlib
import errno
import io
import json
import os
import pathlib
import stat
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
        code, _, error_text = run("bad\x1b[2J\nline\x9b\u061c\u200e\u200f\u202e")
        self.assertEqual(code, 1)
        self.assertIn("bad\\x1b[2J\\nline\\x9b\\u061c\\u200e\\u200f\\u202e", error_text)
        self.assertNotIn("\x1b", error_text)
        self.assertNotIn("\x9b", error_text)
        self.assertNotIn("\u061c", error_text)
        self.assertNotIn("\u200e", error_text)
        self.assertNotIn("\u200f", error_text)
        self.assertNotIn("\u202e", error_text)
        self.assertEqual(failure("greet", "x", "--bogus")[1]["code"], "VALIDATION_FAILED")
        code, error = failure("greet", "x", "--shout", "--shout")
        self.assertIn("twice", error["message"])
        self.assertEqual(failure("greet", "x", "--times", "0")[1]["message"], "Option --times must be between 1 and 10, not 0.")
        self.assertEqual(failure("limits", "--strict")[1]["message"], "Option --strict requires --level.")
        self.assertIn("conflicts with --strict", failure("limits", "--level", "low", "--strict", "--lenient")[1]["message"])
        self.assertIn("required", failure("note", "write", "f")[1]["message"])
        self.assertIn("Operands do not match", failure("greet")[1]["message"])
        code, out, err = run("greet", "x", "--format", "jsonl")
        self.assertEqual((code, out), (1, ""))
        self.assertIn("jsonl", err)

    def test_value_kinds(self) -> None:
        code, out, _ = run("limits", "--memory", "4K", "--wall-time", "2m", "--level", "high", "--offset", "-5",
                           "--digest", "a" * 64, "--tag", "x", "--tag", "y", "--json")
        data = json.loads(out)["data"]
        self.assertEqual((data["memory"], data["wallTimeMs"], data["level"], data["offset"]), (4096, 120000, "high", -5))
        self.assertEqual(data["tags"], ["x", "y"])
        for argv in (("limits", "--memory", "4X"), ("limits", "--wall-time", "5"), ("limits", "--level", "loud"),
                     ("limits", "--offset", "101"), ("limits", "--digest", "zz"), ("limits", "--digest", "a" * 63)):
            self.assertEqual(failure(*argv)[1]["code"], "VALIDATION_FAILED", argv)

    def test_unavailable_code(self) -> None:
        """An unavailable command answers the code that fits its cause, since
        UNSUPPORTED says absence; the declaration refuses an invented code and
        a code without a reason, as the C catalog validation does."""
        program = cli.Program("p", "P", "0", [
            cli.read("absent", "absent", "Absent.", lambda i: ({}, 0),
                     unavailable="built without the backend"),
            cli.read("sealed", "sealed", "Sealed.", lambda i: ({}, 0),
                     unavailable="the component does not match its declared digest",
                     unavailable_code="ACCESS_DENIED"),
        ])
        with self.assertRaises(cli.Failure) as absent:
            program.parse(["absent"])
        self.assertEqual(absent.exception.code, "UNSUPPORTED")
        with self.assertRaises(cli.Failure) as sealed:
            program.parse(["sealed"])
        self.assertEqual(sealed.exception.code, "ACCESS_DENIED")
        self.assertIn("declared digest", sealed.exception.message)
        described = program.descriptor(program.command_by_id("sealed"))
        self.assertEqual(described["available"], False)
        self.assertIn("declared digest", described["unavailableReason"])
        with self.assertRaises(ValueError):
            cli.read("x", "x", "X.", lambda i: ({}, 0), unavailable="r",
                     unavailable_code="DIGEST_MISMATCH")
        with self.assertRaises(ValueError):
            cli.read("x", "x", "X.", lambda i: ({}, 0),
                     unavailable_code="ACCESS_DENIED")

    def test_hex_digits(self) -> None:
        """A hex value states its width as `digits`, the shape the C reference
        emits: --digest of hello.py is described as maelys-hello describes
        its own, a pair of widths accepts either, and a hex declared without
        a width, with a length range, or digits on another kind, is refused
        when the catalog is built."""
        limits = hello.PROGRAM.command_by_id("limits")
        digest = next(o for o in hello.PROGRAM.descriptor(limits)["input"]["options"] if o["long"] == "--digest")
        self.assertEqual(digest["argument"], {"name": "HEX", "type": "hex", "digits": 64})
        program = cli.Program("p", "P", "0", [cli.read("x", "x", "X.", lambda i: ({}, 0),
                                                       operands=[cli.operand("OID", "Either width.", kind="hex",
                                                                             digits=[40, 64])])])
        self.assertEqual(program.descriptor(program.command_by_id("x"))["input"]["operands"][0]["digits"], [40, 64])
        for width in (40, 64):
            self.assertEqual(program.parse(["x", "b" * width])[0].operands[0], "b" * width)
        with self.assertRaises(cli.Failure) as refused:
            program.parse(["x", "b" * 63])
        self.assertIn("40 or 64 lowercase hexadecimal digits", refused.exception.message)
        for keywords in ({}, {"minimum": 64, "maximum": 64}, {"digits": 0}, {"digits": [64, 64]},
                         {"digits": [1, 2, 3]}, {"digits": "64"}):
            with self.assertRaises(ValueError, msg=keywords):
                cli.argument("HEX", "hex", **keywords)
            with self.assertRaises(ValueError, msg=keywords):
                cli.operand("HEX", "H.", kind="hex", **keywords)
        with self.assertRaises(ValueError):
            cli.argument("N", "unsigned", digits=4)

    def test_defaults_come_from_the_catalog(self) -> None:
        code, out, _ = run("greet", "x", "--json")
        self.assertEqual(json.loads(out)["data"]["times"], 1)
        code, out, _ = run("limits", "--json")
        self.assertEqual(json.loads(out)["data"]["level"], "low")

    def test_transaction_plans_then_applies(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "note.txt")
            code, out, _ = run("note", "write", path, "--content", "hi", "--json")
            self.assertEqual((code, json.loads(out)["data"]["mode"]), (0, "plan"))
            self.assertFalse(os.path.exists(path))
            code, out, _ = run("note", "write", path, "--content", "hi", "--apply=false", "--json")
            self.assertEqual((code, json.loads(out)["data"]["mode"]), (0, "plan"))
            self.assertFalse(os.path.exists(path))
            code, error = failure("note", "write", path, "--content", "hi", "--apply=flase")
            self.assertEqual(error["code"], "VALIDATION_FAILED")
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
        code, out, _ = run("list", "--limit", "2")
        self.assertEqual(out, "alpha\t1\nbeta\t2\n")
        self.assertEqual(cli.record_text([{"b": "x\ty", "a": None}, {"c": [1, "2"]}]), 'null\tx\\ty\t\n\t\t[1,"2"]\n')

    def test_field(self) -> None:
        code, out, err = run("describe", "--field", "program")
        self.assertEqual((code, err, out), (0, "", "maelys-hello-py\n"))
        code, out, _ = run("help", "--field", "commands")
        self.assertEqual(out.splitlines()[:3], ["help", "version", "describe"])
        code, out, _ = run("list", "--limit", "2", "--field", "records")
        self.assertEqual(out, "alpha\t1\nbeta\t2\n")
        code, out, _ = run("list", "--limit", "2", "--field", "count")
        self.assertEqual(out, "2\n")
        code, out, _ = run("list", "--limit", "2", "--field", "records", "--format", "jsonl")
        self.assertEqual([json.loads(line)["name"] for line in out.splitlines()], ["alpha", "beta"])
        code, out, err = run("describe", "--field", "program", "--format", "jsonl")
        self.assertEqual((code, err, out), (0, "", '"maelys-hello-py"\n'))
        self.assertEqual(cli.field_text({"a": 1, "b": "x"}), cli.record_text([{"a": 1, "b": "x"}]))
        self.assertEqual(cli.field_jsonl({"a": 1}), '{"a":1}\n')
        self.assertEqual(cli.field_jsonl([1, 2]), "1\n2\n")
        self.assertEqual(cli.field_text(5), "5\n")
        self.assertEqual(cli.field_text([1, "a"]), "1\na\n")
        self.assertEqual(failure("describe", "--field", "no-such-member")[1]["code"], "VALIDATION_FAILED")
        code, body = failure("describe", "--field", "program")
        self.assertEqual((code, body["code"]), (1, "VALIDATION_FAILED"))
        self.assertIn("conflicts with --format json", body["message"])
        self.assertEqual(failure("describe", "--field", "program", "--field", "program")[1]["code"], "VALIDATION_FAILED")

    def test_constraints(self) -> None:
        """The rules a command states itself (spec 2.5): describe carries them
        after the derived entries, the parser enforces them in the slot of the
        dependencies, and exactly-one refuses zero as it refuses two."""
        def policy(invocation: cli.Invocation):
            return {}, cli.EXIT_OK
        program = cli.Program("policy-py", "Policy", "0.0.1", [
            cli.read("policy", "policy", "Choose a policy.", policy,
                     options=[cli.flag("--ro", "Read-only."), cli.flag("--rw", "Read-write."),
                              cli.option("--plan", "From a plan.", cli.argument("FILE", "path")),
                              cli.flag("--trace", "Trace."), cli.flag("--quiet", "Quiet."),
                              cli.flag("--paired-a", "Pair.", group="pair"),
                              cli.flag("--paired-b", "Pair.", group="pair")],
                     constraints=[cli.constraint("exactly-one", "--ro", "--rw", "--plan"),
                                  cli.constraint("at-most-one", "--trace", "--quiet"),
                                  cli.constraint("requires", "--trace", "--plan")]),
        ])

        def local(*argv: str) -> tuple[int, str, str]:
            out, err = io.StringIO(), io.StringIO()
            saved = dict(os.environ)
            os.environ.pop("MAELYS_CLI_FORMAT", None)
            os.environ["NO_COLOR"] = "1"
            try:
                with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                    code = program.main(list(argv))
            finally:
                os.environ.clear()
                os.environ.update(saved)
            return code, out.getvalue(), err.getvalue()

        code, out, _ = local("describe", "policy", "--json")
        constraints = json.loads(out)["data"]["commands"][0]["input"]["constraints"]
        # The derived all-or-none entry has no name; the stated rules follow it.
        self.assertEqual(constraints, [
            {"kind": "all-or-none", "options": ["--paired-a", "--paired-b"]},
            {"kind": "exactly-one", "options": ["--ro", "--rw", "--plan"]},
            {"kind": "at-most-one", "options": ["--trace", "--quiet"]},
            {"kind": "requires", "options": ["--trace", "--plan"]}])
        for argv in (("policy",), ("policy", "--ro", "--rw")):
            code, out, err = local(*argv, "--json")
            self.assertEqual((code, out), (1, ""))
            self.assertIn("Exactly one of --ro, --rw, --plan must be given.", json.loads(err)["error"]["message"])
        self.assertEqual(local("policy", "--rw")[0], 0)
        code, _, err = local("policy", "--ro", "--trace", "--quiet", "--json")
        self.assertIn("At most one of --trace, --quiet may be given.", json.loads(err)["error"]["message"])
        code, _, err = local("policy", "--ro", "--trace", "--json")
        self.assertIn("Option --trace requires --plan.", json.loads(err)["error"]["message"])
        self.assertEqual(local("policy", "--plan", "p", "--trace")[0], 0)
        # One declaration per rule: all-or-none is group=, never a constraint;
        # a rule names at least two distinct options of the command.
        with self.assertRaises(ValueError):
            cli.constraint("all-or-none", "--ro", "--rw")
        with self.assertRaises(ValueError):
            cli.constraint("exactly-one", "--ro")
        with self.assertRaises(ValueError):
            cli.constraint("exactly-one", "--ro", "--ro")
        with self.assertRaises(ValueError):
            cli.constraint("sometimes", "--ro", "--rw")
        with self.assertRaises(ValueError):
            cli.Program("p", "P", "0", [cli.read("x", "x", "X.", policy, options=[cli.flag("--a", "A.")],
                                                 constraints=[cli.constraint("exactly-one", "--a", "--b")])])

    def test_operand_value_members(self) -> None:
        """An operand describes its value exactly as an argument does (spec
        2.6): algorithms for a digest, a pattern for a string, stated by
        describe and enforced by the parser."""
        def named(invocation: cli.Invocation):
            return {}, cli.EXIT_OK
        program = cli.Program("named-py", "Named", "0.0.1", [
            cli.read("named", "named", "A matched label and a digest.", named,
                     operands=[cli.operand("LABEL", "A matched label.", kind="string",
                                           pattern="^[a-z][a-z0-9-]*$"),
                               cli.operand("REF", "A digest.", required=False, kind="digest",
                                           algorithms=["sha256"])]),
        ])

        def local(*argv: str) -> tuple[int, str, str]:
            out, err = io.StringIO(), io.StringIO()
            saved = dict(os.environ)
            os.environ.pop("MAELYS_CLI_FORMAT", None)
            os.environ["NO_COLOR"] = "1"
            try:
                with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                    code = program.main(list(argv))
            finally:
                os.environ.clear()
                os.environ.update(saved)
            return code, out.getvalue(), err.getvalue()

        code, out, _ = local("describe", "named", "--json")
        operands = json.loads(out)["data"]["commands"][0]["input"]["operands"]
        self.assertEqual(operands[0]["pattern"], "^[a-z][a-z0-9-]*$")
        self.assertEqual(operands[1]["algorithms"], ["sha256"])
        code, out, err = local("named", "Label-1", "--json")
        self.assertEqual((code, out), (1, ""))
        self.assertEqual(json.loads(err)["error"]["code"], "VALIDATION_FAILED")
        self.assertEqual(local("named", "label-1")[0], 0)
        code, _, err = local("named", "label-1", "md5:" + "0" * 32, "--json")
        self.assertEqual(json.loads(err)["error"]["code"], "VALIDATION_FAILED")
        self.assertEqual(local("named", "label-1", "sha256:" + "0" * 64)[0], 0)
        with self.assertRaises(ValueError):
            cli.operand("N", "Not a string.", kind="unsigned", pattern="^[0-9]+$")

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
        self.assertTrue(out.startswith("USAGE\n  maelys-hello-py greet NAME"))
        code, out, _ = run("--version", "--json")
        self.assertEqual(json.loads(out)["data"]["version"], hello.VERSION)

    def test_help_layout(self) -> None:
        """The help is laid out as src/app.c lays it out: within eighty columns where stdout is no
        terminal, a description beside a short label and below a long one, a line broken between words,
        a usage between its groups. The general help names a command by its pattern and its purpose;
        the usage is in the command's own help and in its family's."""
        def widest(text: str) -> int:
            return max(cli.display_width(line) for line in text.splitlines())
        guide = run("help")[1]
        self.assertLessEqual(widest(guide), 80)
        self.assertIn("\n  note write  Store a note in a file.\n", guide)
        self.assertNotIn("note write FILE", guide)
        self.assertIn("maelys-hello-py help COMMAND_ID", guide)
        self.assertIn("maelys-hello-py help FAMILY", guide)
        self.assertIn("AGENT CONTRACT", guide)
        # What every program has in common is named, not repeated; `help conventions` has it whole.
        self.assertIn("maelys-hello-py help conventions", guide)
        self.assertIn("--format, --json,", guide)
        self.assertNotIn("Exact alias of --format json", guide)
        self.assertNotIn("Exit 0 is success", guide)
        conventions = run("help", "conventions")[1]
        self.assertLessEqual(widest(conventions), 80)
        self.assertIn("Exact alias of --format json", conventions)
        self.assertIn("Exit 0 is success", conventions)
        self.assertEqual(json.loads(run("help", "conventions", "--json")[1])["data"]["commands"], [])
        # A command of that name is a command first: the topic does not take an identifier away.
        shadowing = cli.Program("p", "P", "0", [cli.read("conventions", "conventions", "A product's own.",
                                                         lambda i: ({}, 0))])
        self.assertTrue(shadowing.command_help(shadowing.command_by_id("conventions")).startswith("USAGE\n  p conventions"))
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(shadowing.main(["help", "conventions"]), 0)
        self.assertIn("A product's own.", out.getvalue())
        self.assertLess(guide.index("\n  greet"), guide.index("\n  describe"))    # the product's commands first
        self.assertNotIn("__complete", guide)
        for identifier in ("limits", "note.write", "describe"):
            self.assertLessEqual(widest(run("help", identifier)[1]), 80, identifier)
        limits = run("help", "limits")[1]
        self.assertIn("EFFECT\n  read\n", limits)
        self.assertIn("OUTPUT\n  json-envelope\n", limits)
        self.assertNotIn("[--offset\n", limits)                                    # a usage breaks between groups
        self.assertIn("--tag TEXT (repeatable)", limits)
        self.assertIn("Requires --level.", limits)
        self.assertIn("preview by default; apply with --apply", run("help", "note.write")[1])
        # A family: `help FAMILY` and `FAMILY --help` say the same, and data.commands lists it.
        code, family, err = run("note", "--help")
        self.assertEqual((code, err), (0, ""))
        self.assertIn("maelys-hello-py note - commands", family)
        self.assertIn("\n  note write FILE --content TEXT", family)
        self.assertIn("\n      Store a note in a file.\n", family)
        self.assertEqual(run("help", "note")[1], family)
        self.assertEqual(json.loads(run("help", "note", "--json")[1])["data"]["commands"], ["note.write"])
        self.assertEqual(failure("note")[1]["code"], "INVALID_COMMAND")              # no --help: still an error
        self.assertEqual(failure("nope", "--help")[1]["code"], "INVALID_COMMAND")
        self.assertIn("identifier or family", failure("help", "nope")[1]["message"])
        # Width is in columns: an accented letter takes one, a CJK character two, a combining mark none.
        self.assertEqual([cli.display_width(text) for text in ("abc", "\u00e9t\u00e9", "e\u0301", "\u65e5\u672c\u8a9e", "")],
                         [3, 3, 1, 6, 0])
        accented = cli.Program("prog", "P", "9.9.9", [
            cli.read("wide", "wide", " ".join(["\u00e9t\u00e9\u00e9t\u00e9"] * 12) + " \u65e5\u672c\u8a9e.",
                     lambda i: ({}, 0))])
        entry = next(line for line in accented.guide().splitlines() if line.startswith("  wide"))
        # The label and its column take 14, nine words of six columns and their spaces 62: the same
        # line src/app.c writes, which tests/test_app.c measures in bytes.
        self.assertEqual((cli.display_width(entry), len(entry.encode("utf-8"))), (76, 112))
        self.assertLessEqual(widest(accented.guide()), 80)

    def test_completion(self) -> None:
        code, out, _ = run("__complete", "--json", "--", "no")
        self.assertEqual([r["word"] for r in json.loads(out)["data"]["records"]], ["note"])
        code, out, _ = run("__complete", "--", "note", "write", "f", "--")
        self.assertIn("--content", out)
        self.assertNotIn("__complete", out)
        code, out, _ = run("completion", "bash")
        self.assertIn("__complete", out)
        self.assertEqual(failure("completion", "ksh")[1]["code"], "VALIDATION_FAILED")

    def test_rendering_refusal_precedes_the_run(self) -> None:
        """--field against the format MAELYS_CLI_FORMAT selects is refused before the handler, as the
        explicit --format json is by the parser: a transaction is not applied and then refused."""
        with tempfile.TemporaryDirectory() as directory:
            target = os.path.join(directory, "note.txt")
            for extra in ((), ("--compact",)):
                code, out, err = run("note", "write", target, "--content", "hi", "--apply", "--field", "path",
                                     *extra, env={"MAELYS_CLI_FORMAT": "json"})
                self.assertEqual((code, out), (1, ""))
                self.assertIn("--field conflicts with --format json", err)
                self.assertFalse(os.path.exists(target), "the note was written, then the rendering refused")
            # A command that can write accepts for --field only a member its output schema requires,
            # and says so before it runs (spec 2.9, section 5): the plan and the application alike.
            for apply in ((), ("--apply",)):
                code, out, err = run("note", "write", target, "--content", "hi", *apply, "--field", "no-such-member")
                self.assertEqual((code, out), (1, ""))
                self.assertIn("which 'note.write' does not always return", err)
                self.assertFalse(os.path.exists(target))
            # A read decides on data, as before.
            self.assertIn("which 'describe' does not have", run("describe", "--field", "no-such-member")[2])
            # An explicit format overrides the environment's default, and the command runs.
            code, out, _ = run("note", "write", target, "--content", "hi", "--apply", "--field", "path",
                               "--format", "text", env={"MAELYS_CLI_FORMAT": "json"})
            self.assertEqual((code, out.strip()), (0, target))
            self.assertTrue(os.path.exists(target))

    def test_field_on_a_command_that_can_write(self) -> None:
        """What --field accepts on a transaction or an execute is read in the catalog: the top-level
        `required` of the output schema, and nothing when it requires nothing. The handler has not run."""
        ran = []

        def handler(invocation: cli.Invocation) -> "tuple[dict, int]":
            ran.append(invocation.command["id"])
            return {"mode": "plan", "extra": 1}, cli.EXIT_OK
        program = cli.Program("p", "P", "0", [
            cli.transaction("strict", "strict", "Requires mode.", handler,
                            schema={"type": "object", "required": ["mode"]}),
            cli.transaction("loose", "loose", "Requires nothing.", handler),
            cli.execute("act", "act", "Requires nothing.", handler),
            cli.read("look", "look", "A read.", handler),
        ])

        def answer(*argv: str) -> "tuple[int, str, str]":
            out, err = io.StringIO(), io.StringIO()
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                code = program.main(list(argv))
            return code, out.getvalue(), err.getvalue()
        self.assertEqual(answer("strict", "--field", "mode")[:2], (0, "plan\n"))
        self.assertEqual(ran, ["strict"])
        # `extra` is in data on every run of this handler, and is refused: the schema does not require it.
        for argv in (("strict", "--field", "extra"), ("strict", "--apply", "--field", "extra")):
            code, out, err = answer(*argv)
            self.assertEqual((code, out), (1, ""))
            self.assertIn("names 'extra', which 'strict' does not always return", err)
        for command in ("loose", "act"):
            code, out, err = answer(command, "--field", "mode")
            self.assertEqual((code, out), (1, ""))
            self.assertIn("its output schema requires no member", err)
        self.assertEqual(ran, ["strict"])                 # none of the refused runs reached the handler
        self.assertEqual(answer("look", "--field", "extra")[:2], (0, "1\n"))

    def test_expect_binds_apply_to_the_plan(self) -> None:
        """transaction(expect=True) declares the reserved --expect FINGERPRINT (spec 2.9, section 4): the
        plan carries a fingerprint over the action and the state it touches, and --apply --expect applies
        only the plan that fingerprint names, refusing any other before anything is written."""
        with tempfile.TemporaryDirectory() as directory:
            target = os.path.join(directory, "note.txt")
            reviewed = run("note", "write", target, "--content", "first", "--field", "fingerprint")[1].strip()
            self.assertRegex(reviewed, r"^sha256:[0-9a-f]{64}$")
            self.assertEqual(run("note", "write", target, "--content", "first", "--field", "fingerprint")[1].strip(), reviewed)
            self.assertNotEqual(run("note", "write", target, "--content", "second", "--field", "fingerprint")[1].strip(),
                                reviewed)
            code, error = failure("note", "write", target, "--content", "second", "--apply", "--expect", reviewed)
            self.assertEqual((code, error["code"]), (1, "PRECONDITION_FAILED"))
            self.assertIn("Plan again without --apply", error["hint"])
            self.assertFalse(os.path.exists(target))
            pathlib.Path(target).write_text("someone else")           # the state moves under the same action
            code, error = failure("note", "write", target, "--content", "first", "--replace", "--apply",
                                  "--expect", reviewed)
            self.assertEqual(error["code"], "PRECONDITION_FAILED")
            self.assertEqual(pathlib.Path(target).read_text(), "someone else")
            os.unlink(target)
            code, out, _ = run("note", "write", target, "--content", "first", "--apply", "--expect", reviewed, "--json")
            self.assertEqual((code, json.loads(out)["data"]["fingerprint"]), (0, reviewed))
            self.assertEqual(pathlib.Path(target).read_text(), "first")
            self.assertIn("requires --apply",
                          failure("note", "write", target, "--content", "x", "--replace", "--expect", reviewed)[1]["message"])
        described = hello.PROGRAM.descriptor(hello.PROGRAM.command_by_id("note.write"))
        expect = next(item for item in described["input"]["options"] if item["long"] == "--expect")
        self.assertEqual((expect["argument"]["type"], expect["argument"]["algorithms"], expect["requires"]),
                         ("digest", ["sha256"], ["--apply"]))
        self.assertIn("fingerprint", described["outputSchema"]["required"])

        # The fingerprint is the framing of the C library, byte for byte: the same reference strings
        # as tests/test_digest.c, computed apart from both.
        self.assertEqual(cli.Fingerprint().finish(),
                         "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
        self.assertEqual(cli.Fingerprint().add("ab", "c").finish(),
                         "sha256:98567ed2582c877b4f71760c31e078ab7d7bd86f42689d58325689d652f4056a")
        self.assertEqual(cli.Fingerprint().add("a", b"bc").finish(),
                         "sha256:890cb9913f8086a050b29183b982ee91719b35a2f82cfb9b00a01d6c9884031d")
        self.assertEqual(cli.Fingerprint().add("k", None).finish(),
                         "sha256:d815ac6f89858e27e4a82ec5e72a64929b510196736898158ab4b48c00410c3d")
        self.assertEqual(cli.Fingerprint().add("k", "").finish(),
                         "sha256:629d67d8c50c9d34279f5c01951a2ccdbe46930ac5da8e5b9ac52205f60a0a45")
        self.assertEqual(cli.Fingerprint().add("path", "/tmp/x").add("content", "hi").finish(),
                         "sha256:aeff1db4e0dd584988c9a8a30434603418a6ba8813483d00ef7227191cea74a0")
        with tempfile.TemporaryDirectory() as directory:
            note = os.path.join(directory, "note")
            self.assertEqual(cli.Fingerprint().add_file("k", note, 16).finish(), cli.Fingerprint().add("k", None).finish())
            pathlib.Path(note).write_text("hi")
            self.assertEqual(cli.Fingerprint().add_file("target", note, 16).finish(),
                             "sha256:a20aaeef176c63d62a064d2db7831d3b1cce5a3e99b5369f0ce16dc985f51fbd")
            with self.assertRaises(OSError):
                cli.Fingerprint().add_file("target", note, 1)          # too large
            with self.assertRaises(OSError):
                cli.Fingerprint().add_file("target", directory, 16)    # not a file

        # The declaration is whole or refused, as the C catalog validation refuses it.
        def handler(invocation: cli.Invocation) -> "tuple[dict, int]":
            return {"mode": "apply", "fingerprint": "sha256:" + "0" * 64}, cli.EXIT_OK
        bound = {"type": "object", "required": ["mode", "fingerprint"]}
        cli.transaction("t", "t", "T.", handler, expect=True, schema=bound)
        with self.assertRaises(ValueError):
            cli.transaction("t", "t", "T.", handler, expect=True, schema={"type": "object", "required": ["mode"]})
        with self.assertRaises(ValueError):
            cli.transaction("t", "t", "T.", handler, expect=True)
        with self.assertRaises(ValueError):                             # another meaning of the name
            cli.transaction("t", "t", "T.", handler, schema=bound,
                            options=[cli.option("--expect", "Else.", cli.argument("TEXT"))])
        cli.read("r", "r", "R.", handler, options=[cli.option("--expect", "Free on a read.", cli.argument("TEXT"))])

        # A handler that answers without having asked invocation.expect() is not believed.
        careless = cli.Program("p", "P", "0", [cli.transaction("t", "t", "T.", handler, expect=True, schema=bound)])
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = careless.main(["t", "--apply", "--expect", "sha256:" + "0" * 64])
        self.assertEqual((code, out.getvalue()), (1, ""))
        self.assertIn("[UNEXPECTED]", err.getvalue())
        self.assertIn("answered without checking --expect", err.getvalue())
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(careless.main(["t", "--apply"]), 0)

    def test_completion_scripts(self) -> None:
        """The scripts that call __complete print what src/app.c prints (make hello-parity-check compares
        them whole); each line here is one 0.5.33 got wrong."""
        bash = hello.PROGRAM.completion_script("bash", static=False)
        # bash 3.2 joins the slice into one word once IFS is a newline: the words are taken first.
        self.assertLess(bash.index('words=("${COMP_WORDS[@]:1:COMP_CWORD}")'), bash.index("local IFS=$'\\n'"))
        self.assertIn("complete -o filenames -F _maelys_hello_py_complete maelys-hello-py", bash)
        zsh = hello.PROGRAM.completion_script("zsh", static=False)
        # An empty answer is an empty array, so _files is reached; the words stop at the cursor.
        self.assertIn('candidates=(${(f)"$("maelys-hello-py" __complete -- "${(@)words[2,CURRENT]}" 2>/dev/null)"})', zsh)
        self.assertNotIn("(@f)", zsh)
        # Sourced after compinit or autoloaded from fpath under the name of its #compdef line.
        self.assertIn("if [[ ${funcstack[1]} == _maelys-hello-py ]]; then", zsh)
        fish = hello.PROGRAM.completion_script("fish", static=False)
        self.assertIn('$words[2..-1] "$current"', fish)
        self.assertIn('__fish_complete_path "$current"', fish)

    def test_static_completion(self) -> None:
        """`completion SHELL` prints a script that carries the candidates of the catalog and its version,
        and calls the program only after a delegate's pattern; make completion-check drives the three in
        their shell against __complete. A catalog with a word that would not be inert in a shell, and a
        program that asks for it, get the script that calls __complete."""
        for shell in ("bash", "zsh", "fish"):
            printed = run("completion", shell)[1]
            self.assertEqual(printed, hello.PROGRAM.completion_script(shell))
            self.assertIn(f"completion for maelys-hello-py {hello.VERSION}, generated from its catalog", printed)
            self.assertIn("'C|6|note write|note.write|c'", printed)
            self.assertIn("'O|5|--level|ae|low high'", printed)
            self.assertIn("'O|5|--tag|ar|'", printed)
            self.assertIn("'O|4|--trace|h|'", printed)             # hidden: carried, to be skipped
            self.assertIn("'O|g|--format|ae|text json jsonl'", printed)
            self.assertNotIn("|d'", printed)                       # no delegate here: nothing asks the program
            self.assertNotIn("complete.candidates", printed)       # a hidden command is not carried
            self.assertNotEqual(printed, hello.PROGRAM.completion_script(shell, static=False))
        self.assertEqual(json.loads(run("completion", "zsh", "--json")[1])["data"]["script"],
                         hello.PROGRAM.completion_script("zsh"))
        with self.assertRaises(ValueError):
            hello.PROGRAM.completion_script("ksh")

        def program(**keywords: object) -> cli.Program:
            return cli.Program("p", "P", "1.0", [
                cli.external("tool", "tool", "A delegate.", lambda i: 0),
                cli.stream("pipe", "pipe", "A stream.", lambda i: 0),
                cli.read("absent", "absent", "Absent.", lambda i: ({}, 0), unavailable="built without it"),
                cli.read("pick", "pick", "Pick.", lambda i: ({}, 0),
                         operands=[cli.operand("REST", "Rest.", required=False, variadic=True,
                                               choices=keywords.pop("choices", ["a", "b"]))]),
            ], **keywords)
        carried = program().completion_script("bash")
        self.assertIn("'C|4|tool|tool|d'", carried)                # a delegate: the script asks __complete
        self.assertIn('"p" __complete -- "${prev[@]}" "$cur"', carried)
        self.assertIn("'C|5|pipe|pipe|s'", carried)                # a stream: no shared option
        self.assertIn("'P|6|v|a b'", carried)
        self.assertNotIn("absent", carried)
        self.assertNotIn("|4|--", carried)                         # nothing of a delegate's is carried
        # One word outside the inert set, and the whole script is the one that calls __complete.
        for unsafe in ("it's", "a b", "$(x)", "a|b", "*", "\\"):
            asked = program(choices=["a", unsafe]).completion_script("bash")
            self.assertNotIn("_p_rows", asked, unsafe)
            self.assertIn('COMPREPLY=($("p" __complete -- "${words[@]}" 2>/dev/null))', asked)
        declined = program(static_completion=False)
        self.assertEqual(declined.completion_script("fish"), declined.completion_script("fish", static=False))
        self.assertIn("_p_rows", declined.completion_script("bash", static=True))

    def test_completion_candidates(self) -> None:
        """__complete follows builtin_complete of src/app.c: the same words in the same order."""
        def words(*given: str) -> list:
            return run("__complete", "--", *given)[1].split()
        self.assertEqual(words("greet", ""), [])                       # free text: the shell offers files
        self.assertEqual(words("greet", "-"), [])                      # options are offered after --
        self.assertEqual(words("greet", "--sh"), ["--shout"])
        self.assertEqual(words("limits", "--level", ""), ["low", "high"])
        self.assertEqual(words("limits", "--level=h"), ["--level=high"])
        self.assertEqual(words("greet", "--format", "j"), ["json", "jsonl"])
        self.assertEqual(words("completion", ""), ["bash", "zsh", "fish"])
        self.assertEqual(words("help", "no"), ["note.write"])
        self.assertEqual(words("limits", "--tag", "a", "--ta"), ["--tag"])     # repeatable: offered again
        self.assertEqual(words("limits", "--level", "low", "--lev"), [])       # given once: not again
        program = cli.Program("p", "P", "0", [
            cli.read("pair.one", "pair one", "One.", lambda i: ({}, 0)),
            cli.read("pair.two", "pair two", "Two.", lambda i: ({}, 0)),
            cli.read("absent", "absent", "Absent.", lambda i: ({}, 0), unavailable="built without it"),
            cli.external("tool", "tool", "A delegate.", lambda i: 0),
            cli.stream("pipe", "pipe", "A stream.", lambda i: 0, operands=[cli.operand("TARGET", "Target.")]),
        ])

        def offered(*given: str) -> list:
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(program.main(["__complete", "--", *given]), 0)
            return out.getvalue().split()
        self.assertEqual(offered("pa"), ["pair"])                      # one word for two commands
        self.assertEqual(offered("pair", ""), ["one", "two"])
        self.assertNotIn("absent", offered(""))
        self.assertNotIn("absent", offered("help", ""))                # nor as an identifier
        self.assertIn("pair.one", offered("help", ""))
        # After a delegate's pattern the words are the delegate's: none here, never this program's options.
        self.assertEqual(offered("tool", ""), [])
        self.assertEqual(offered("tool", "--"), [])
        self.assertEqual(offered("pipe", "x", "--"), [])               # a stream takes no rendering option

    def test_text_failure_and_format_environment(self) -> None:
        code, out, err = run("greet")
        self.assertEqual(code, 1)
        self.assertTrue(err.startswith("maelys-hello-py: [VALIDATION_FAILED]"))
        self.assertIn("Hint:", err)
        code, out, err = run("greet", env={"MAELYS_CLI_FORMAT": "json"})
        self.assertEqual(json.loads(err)["error"]["code"], "VALIDATION_FAILED")

    def test_pattern_is_enforced_and_prefix_is_validated(self) -> None:
        program = cli.Program("p", "P", "1", [cli.read("tag", "tag", "x", lambda i: ({"v": i.option("--name")}, 0),
                                                   options=[cli.option("--name", "n", cli.argument("NAME", "string", pattern="^[a-z]+$"))])])
        invocation, _ = program.parse(["tag", "--name", "matching"])
        self.assertEqual(invocation.option("--name"), "matching")
        with self.assertRaises(cli.Failure) as caught:
            program.parse(["tag", "--name", "NOT-matching"])
        self.assertEqual(caught.exception.code, "VALIDATION_FAILED")
        self.assertIn("a value matching ^[a-z]+$", caught.exception.message)
        with self.assertRaises(ValueError):
            cli.Program("p", "P", "1", [cli.read("t", "t", "x", lambda i: ({}, 0),
                                              options=[cli.option("--n", "n", cli.argument("N", "string", pattern="^([a-z]+$"))])])
        code, out, _ = run("describe", "describe", "--json")
        prefix = next(o for o in json.loads(out)["data"]["commands"][0]["input"]["options"] if o["long"] == "--prefix")
        self.assertEqual(prefix["argument"]["pattern"], cli.PREFIX_GRAMMAR.pattern)
        code, error = failure("describe", "--summary", "--prefix", "Bad.")
        self.assertEqual(error["code"], "VALIDATION_FAILED")
        code, error = failure("describe", "--summary", "--prefix", "")
        self.assertEqual(error["code"], "VALIDATION_FAILED")
        self.assertEqual(failure("describe", "--summary", "--prefix", "zzz")[1]["message"], "No command in namespace: zzz.")

    def test_trunk_diagnostics_and_pager(self) -> None:
        code, out, err = run("version", "--verbose", "--progress", "always", "--pager", "always", "--json", "--compact")
        self.assertEqual((code, err), (0, ""))
        self.assertTrue(json.loads(out)["ok"])
        _, plain, _ = run("greet", "Ada")
        code, out, err = run("greet", "Ada", "--verbose", "--progress", "always")
        self.assertEqual((code, out), (0, plain))
        self.assertIn("maelys-hello-py: greeting Ada 1 time(s)\n", err)
        self.assertFalse(any(line.startswith("maelys-hello-py: [") for line in err.splitlines()))
        code, out, err = run("greet", "Ada", "--verbose=false", "--progress=never", "--pager=never")
        self.assertEqual((code, out, err), (0, plain, ""))
        self.assertEqual(failure("version", "--verbose", "--verbose")[1]["code"], "VALIDATION_FAILED")
        self.assertEqual(failure("version", "--pager=sometimes")[1]["code"], "VALIDATION_FAILED")
        code, out, _ = run("describe", "--json")
        longs = [o["long"] for o in json.loads(out)["data"]["globalOptions"]]
        self.assertEqual(longs[-5:], ["--progress", "--verbose", "--pager", "--field", "--help"])
        code, out, _ = run("describe", "--summary", "--json")
        self.assertNotIn("globalOptions", json.loads(out)["data"])
        # No pager in a pipe, whatever PAGER names.
        with tempfile.TemporaryDirectory() as directory:
            marker = os.path.join(directory, "started")
            code, out, err = run("version", "--pager", "always",
                                 env={"PAGER": f"sh -c 'touch {marker}; cat'"})
            self.assertEqual((code, out, err), (0, "maelys-hello-py 0.1.0\n", ""))
            self.assertFalse(os.path.exists(marker))
        self.assertEqual(cli.pager_command.__name__, "pager_command")
        saved = os.environ.get("PAGER")
        try:
            os.environ["PAGER"] = "  "
            self.assertIsNone(cli.pager_command())
            os.environ["PAGER"] = "less -R 'a b'"
            self.assertEqual(cli.pager_command(), ["less", "-R", "a b"])
            os.environ["PAGER"] = "less 'unterminated"
            self.assertIsNone(cli.pager_command())
            os.environ.pop("PAGER")
            self.assertEqual(cli.pager_command(), ["less"])
        finally:
            if saved is None:
                os.environ.pop("PAGER", None)
            else:
                os.environ["PAGER"] = saved

    def test_color(self) -> None:
        # terminal_color() is the resolution behind Invocation.color_stdout/
        # color_stderr, mirroring maelys_cli_terminal_detect(): never wins; always
        # or CLICOLOR_FORCE force both streams; otherwise isatty() decides, false
        # for both under this test harness (redirected to io.StringIO).
        self.assertEqual(cli.terminal_color("never"), (False, False))
        self.assertEqual(cli.terminal_color("always"), (True, True))
        self.assertEqual(cli.terminal_color("auto"), (False, False))
        saved = os.environ.get("CLICOLOR_FORCE")
        try:
            os.environ["CLICOLOR_FORCE"] = "1"
            self.assertEqual(cli.terminal_color("auto"), (True, True))
            self.assertEqual(cli.terminal_color("never"), (False, False))
        finally:
            if saved is None:
                os.environ.pop("CLICOLOR_FORCE", None)
            else:
                os.environ["CLICOLOR_FORCE"] = saved
        # A resolved Invocation exposes the same decision; a handler reads it
        # instead of re-scanning sys.argv or the environment.
        program = cli.Program("p", "P", "1", [cli.read("c", "c", "x", lambda i:
            ({"stdout": i.color_stdout, "stderr": i.color_stderr}, 0))])
        invocation, _ = program.parse(["c", "--color", "always"])
        self.assertEqual((invocation.color, invocation.color_stdout, invocation.color_stderr),
                         ("always", True, True))
        invocation, _ = program.parse(["c"])
        self.assertEqual(invocation.color, "auto")
        self.assertFalse(invocation.color_stdout or invocation.color_stderr)
        # The runtime's own failure rendering: a resolved invocation honors
        # --color always even off a terminal.
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "note.txt")
            run("note", "write", path, "--content", "hi", "--apply", "--json")
            code, out, err = run("note", "write", path, "--content", "again", "--color", "always")
            self.assertEqual(code, 1)
            self.assertIn("\033[31m", err)
            self.assertIn("[PRECONDITION_FAILED]", err)
        # An unresolved command line does not honor --color always (the C
        # prescan's asymmetry), but does honor an explicit --color never.
        code, out, err = run("no-such-command", "--color", "always")
        self.assertEqual(code, 1)
        self.assertNotIn("\033[31m", err)
        code, out, err = run("no-such-command", "--color", "never")
        self.assertEqual(code, 1)
        self.assertNotIn("\033[31m", err)

    def test_format_environment_ignores_unknown_values(self) -> None:
        code, out, err = run("greet", env={"MAELYS_CLI_FORMAT": "xml"})
        self.assertTrue(err.startswith("maelys-hello-py: [VALIDATION_FAILED]"))
        code, out, err = run("greet", "x", env={"MAELYS_CLI_FORMAT": "json"})
        self.assertEqual(json.loads(out)["command"], "greet")

    def test_os_errors_map_to_the_stable_codes(self) -> None:
        self.assertEqual(cli.file_error_code(errno.ENOENT), "NOT_FOUND")
        self.assertEqual(cli.file_error_code(errno.ENOTDIR), "NOT_FOUND")
        self.assertEqual(cli.file_error_code(errno.EACCES), "ACCESS_DENIED")
        self.assertEqual(cli.file_error_code(errno.EPERM), "ACCESS_DENIED")
        for number in (errno.EFBIG, errno.ELOOP, errno.EMLINK, errno.EINVAL, errno.EISDIR, cli.EFTYPE):
            self.assertEqual(cli.file_error_code(number), "VALIDATION_FAILED")
        self.assertEqual(cli.file_error_code(errno.EIO), "IO_FAILED")
        self.assertEqual(cli.file_error_code(0), "IO_FAILED")

        def missing(invocation):
            with open("/nonexistent/maelys", "rb"):
                pass
        program = cli.Program("p", "P", "1", [cli.read("m", "m", "x", missing)])
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            code = program.main(["m", "--json"])
        error = json.loads(err.getvalue())["error"]
        self.assertEqual((code, error["code"]), (1, "NOT_FOUND"))
        self.assertTrue(error["message"].startswith("/nonexistent/maelys: "))
        failure_ = cli.file_failure(cli.FileError(errno.EPERM, "file is not owned by the caller", "s"), "secret")
        self.assertEqual((failure_.code, failure_.message, failure_.hint),
                         ("ACCESS_DENIED", f"secret: {os.strerror(errno.EPERM)}", "File is not owned by the caller."))

    def test_trusted_files(self) -> None:
        secret = cli.FILE_NO_SYMLINK | cli.FILE_OWNER_CALLER | cli.FILE_PRIVATE | cli.FILE_SINGLE_LINK
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "secret")
            cli.write_file_atomic(path, b"hunter2", 0o600, cli.WRITE_NO_REPLACE)
            with self.assertRaises(cli.FileError) as caught:
                cli.write_file_atomic(path, b"x", 0o600, cli.WRITE_NO_REPLACE)
            self.assertEqual(caught.exception.errno, errno.EEXIST)
            cli.write_file_atomic(path, b"hunter2", 0o600, cli.WRITE_REPLACE)
            self.assertEqual(stat.S_IMODE(os.stat(path).st_mode), 0o600)
            self.assertEqual(sorted(os.listdir(directory)), ["secret"])
            buffer = cli.read_trusted_file(path, secret, 1, 64)
            self.assertEqual(bytes(buffer), b"hunter2")
            cli.zero(buffer)
            self.assertEqual(bytes(buffer), bytes(7))
            self.assertEqual(bytes(cli.read_trusted_file(path, 0, 7, 7)), b"hunter2")
            cli.check_file(path, secret)
            for minimum, maximum, explanation in ((0, 6, "larger"), (8, 64, "smaller")):
                with self.assertRaises(cli.FileError) as caught:
                    cli.read_trusted_file(path, 0, minimum, maximum)
                self.assertEqual(caught.exception.errno, errno.EFBIG)
                self.assertIn(explanation, caught.exception.explanation)
            with self.assertRaises(cli.FileError) as caught:
                cli.read_trusted_file(path, 0, 9, 8)
            self.assertEqual(caught.exception.errno, errno.EINVAL)
            # Symbolic link: refused with NO_SYMLINK, followed and judged without.
            link = os.path.join(directory, "link")
            os.symlink(path, link)
            with self.assertRaises(cli.FileError) as caught:
                cli.read_trusted_file(link, cli.FILE_NO_SYMLINK, 0, 64)
            self.assertEqual(caught.exception.errno, errno.ELOOP)
            self.assertEqual(bytes(cli.read_trusted_file(link, cli.FILE_PRIVATE, 0, 64)), b"hunter2")
            with self.assertRaises(cli.FileError):
                cli.check_file(link, cli.FILE_NO_SYMLINK)
            # FILE_TRUSTED_DIRECTORY: the link is followed and the directory it
            # resolves to must be the caller's and closed to group and world.
            # What the extension loader asks of a manifest a package manager
            # linked into its prefix.
            manifest = cli.FILE_REGULAR | cli.FILE_OWNER_TRUSTED | \
                cli.FILE_NOT_WRITABLE_BY_OTHERS | cli.FILE_TRUSTED_DIRECTORY
            self.assertEqual(bytes(cli.read_trusted_file(link, manifest, 0, 64)), b"hunter2")
            cli.check_file(link, cli.FILE_TRUSTED_DIRECTORY)
            cli.check_file(path, cli.FILE_TRUSTED_DIRECTORY)
            os.chmod(directory, 0o777)
            for judge in (lambda: cli.read_trusted_file(link, manifest, 0, 64),
                          lambda: cli.check_file(link, cli.FILE_TRUSTED_DIRECTORY),
                          lambda: cli.check_file(path, cli.FILE_TRUSTED_DIRECTORY)):
                with self.assertRaises(cli.FileError) as caught:
                    judge()
                self.assertEqual(caught.exception.errno, errno.EPERM)
                self.assertIn("directory", caught.exception.explanation)
            os.chmod(directory, 0o700)
            os.unlink(link)
            # A dangling link resolves to nothing, whatever is asked of it.
            os.symlink(os.path.join(directory, "absent"), link)
            with self.assertRaises(cli.FileError):
                cli.check_file(link, cli.FILE_TRUSTED_DIRECTORY)
            os.unlink(link)
            # Hard link, permissions, FIFO, directory, missing, empty path.
            alias = os.path.join(directory, "alias")
            os.link(path, alias)
            with self.assertRaises(cli.FileError) as caught:
                cli.read_trusted_file(path, cli.FILE_SINGLE_LINK, 0, 64)
            self.assertEqual(caught.exception.errno, errno.EMLINK)
            os.unlink(alias)
            os.chmod(path, 0o644)
            with self.assertRaises(cli.FileError) as caught:
                cli.open_trusted(path, cli.FILE_PRIVATE)
            self.assertEqual((caught.exception.errno, cli.file_error_code(caught.exception.errno)),
                             (errno.EPERM, "ACCESS_DENIED"))
            descriptor = cli.open_trusted(path, cli.FILE_NOT_WRITABLE_BY_OTHERS)
            self.assertEqual(os.get_inheritable(descriptor), False)
            self.assertEqual(os.get_blocking(descriptor), True)
            os.close(descriptor)
            fifo = os.path.join(directory, "fifo")
            os.mkfifo(fifo, 0o600)
            with self.assertRaises(cli.FileError) as caught:
                cli.open_trusted(fifo, 0)
            self.assertEqual(caught.exception.errno, cli.EFTYPE)
            os.unlink(fifo)
            with self.assertRaises(cli.FileError) as caught:
                cli.read_regular_file(directory, 0, 64)
            self.assertEqual(caught.exception.errno, cli.EFTYPE)
            with self.assertRaises(cli.FileError) as caught:
                cli.read_regular_file("/nonexistent/maelys", 0, 64)
            self.assertEqual(caught.exception.errno, errno.ENOENT)
            with self.assertRaises(cli.FileError) as caught:
                cli.read_regular_file("", 0, 64)
            self.assertEqual(caught.exception.errno, errno.EINVAL)
            # An empty file within bounds, and a growing file bounded by the bytes read.
            cli.write_file_atomic(path, b"", 0o600, cli.WRITE_REPLACE)
            self.assertEqual(bytes(cli.read_trusted_file(path, 0, 0, 8)), b"")
            cli.write_file_atomic(path, b"12345678", 0o600, cli.WRITE_REPLACE)
            descriptor = cli.open_trusted(path, 0)
            with open(path, "ab") as handle:
                handle.write(b"9")
            with self.assertRaises(cli.FileError) as caught:
                cli._read_bounded(descriptor, 8, path)
            self.assertEqual(caught.exception.errno, errno.EFBIG)
            os.close(descriptor)
            self.assertEqual(len(cli.read_trusted_file(path, 0, 0, 9)), 9)
            # The transaction of the reference product writes atomically and refuses to replace.
            note = os.path.join(directory, "note.txt")
            run("note", "write", note, "--content", "hi", "--apply", "--json")
            self.assertEqual(pathlib.Path(note).read_text(), "hi")
            self.assertEqual(sorted(os.listdir(directory)), ["note.txt", "secret"])

    def test_synopsis_override(self) -> None:
        program = cli.Program("p", "P", "1", [cli.read("adopt", "adopt", "x", lambda i: ({}, 0),
                                                   operands=[cli.operand("DIR", "d")],
                                                   options=[cli.flag("--apply", "a"), cli.option("--socle-sha", "trial", cli.argument("SHA", "hex", digits=2))],
                                                   synopsis="adopt DIR [--apply]")])
        command = program.command_by_id("adopt")
        self.assertEqual(command["usage"], "adopt DIR [--apply]")
        self.assertEqual([o["long"] for o in program.descriptor(command)["input"]["options"]], ["--apply", "--socle-sha"])
        self.assertEqual(program.descriptor(command)["input"]["synopsis"], command["usage"])
        invocation, _ = program.parse(["adopt", "here", "--socle-sha", "ab"])
        self.assertEqual(invocation.option("--socle-sha"), "ab")
        with self.assertRaises(ValueError):
            cli.read("a", "a", "x", lambda i: ({}, 0), synopsis="b [--x]")

    def test_hidden_option(self) -> None:
        code, out, _ = run("describe", "greet", "--json")
        descriptor = json.loads(out)["data"]["commands"][0]
        trace = next(o for o in descriptor["input"]["options"] if o["long"] == "--trace")
        self.assertIs(trace["hidden"], True)
        self.assertNotIn("hidden", next(o for o in descriptor["input"]["options"] if o["long"] == "--shout"))
        self.assertNotIn("--trace", descriptor["usage"])
        code, out, _ = run("help", "greet")
        self.assertIn("--shout", out)
        self.assertNotIn("--trace", out)
        code, out, _ = run("__complete", "--", "greet", "--")
        self.assertIn("--shout", out)
        self.assertNotIn("--trace", out)
        code, out, err = run("greet", "x", "--trace", "--json")
        self.assertEqual((code, json.loads(out)["data"]["greeting"]), (0, "Hello, x!"))
        self.assertIn("warning: greet: name=x", err)
        with self.assertRaises(ValueError):
            cli.flag("--x", "x", hidden=True, required=True)

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
