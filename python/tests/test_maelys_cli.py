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
        code, _, error_text = run("bad\x1b[2J\nline\x9b\u202e")
        self.assertEqual(code, 1)
        self.assertIn("bad\\x1b[2J\\nline\\x9b\\u202e", error_text)
        self.assertNotIn("\x1b", error_text)
        self.assertNotIn("\x9b", error_text)
        self.assertNotIn("\u202e", error_text)
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

    def test_pattern_is_informative_and_prefix_is_validated(self) -> None:
        program = cli.Program("p", "P", "1", [cli.read("tag", "tag", "x", lambda i: ({"v": i.option("--name")}, 0),
                                                   options=[cli.option("--name", "n", cli.argument("NAME", "string", pattern="^[a-z]+$"))])])
        invocation, _ = program.parse(["tag", "--name", "NOT-matching"])
        self.assertEqual(invocation.option("--name"), "NOT-matching")
        code, out, _ = run("describe", "describe", "--json")
        prefix = next(o for o in json.loads(out)["data"]["commands"][0]["input"]["options"] if o["long"] == "--prefix")
        self.assertEqual(prefix["argument"]["pattern"], cli.PREFIX_GRAMMAR.pattern)
        code, error = failure("describe", "--summary", "--prefix", "Bad.")
        self.assertEqual(error["code"], "VALIDATION_FAILED")
        code, error = failure("describe", "--summary", "--prefix", "")
        self.assertEqual(error["code"], "VALIDATION_FAILED")
        self.assertEqual(failure("describe", "--summary", "--prefix", "zzz")[1]["message"], "No command in namespace: zzz.")

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
                                                   options=[cli.flag("--apply", "a"), cli.option("--socle-sha", "trial", cli.argument("SHA", "hex"))],
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
