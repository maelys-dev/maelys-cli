# SPDX-License-Identifier: MPL-2.0
"""maelys_cli: the agent-cli/v2 contract for a Python command-line product.

The Python counterpart of libmaelys_cli: one declarative catalog drives the
parser, `help`, `describe`, the shell completion and `__complete`; success
is a JSON envelope on stdout with --format json, failure an envelope on
stderr; exit 0 completed, 1 failed, 2 a validation that found violations.
Transactions plan by default and write with --apply; --dry-run and --plan
are refused. Standard library only, Python 3.9 or later, one file: a
product copies it next to its program or puts it on its path, pinned by
adapter/MAELYS_CLI_PIN like the C library.

    import maelys_cli as cli

    def greet(invocation):
        return {"greeting": f"Hello, {invocation.operands[0]}"}, cli.EXIT_OK

    PROGRAM = cli.Program("hello", "Hello", "0.1.0", [
        cli.read("greet", "greet", "Greet someone.", greet,
                 operands=[cli.operand("NAME", "Who to greet.")],
                 options=[cli.flag("--shout", "Capitals.")],
                 schema={"type": "object", "required": ["greeting"]}),
    ])
    sys.exit(PROGRAM.main())

A handler receives an Invocation and returns (data, exit_code), or raises
Failure(code, message, hint). The value kinds, the causal order of errors,
the built-in commands and the envelopes are those of the contract; the
conformance kit of maelys-dev/agent-cli-spec checks a program built on this
module from the outside.
"""
from __future__ import annotations

import json
import os
import re
import sys
from typing import Any, Callable, Optional

CONTRACT = "agent-cli/v2"
SCHEMA_VERSION = 2
CATALOG_SCHEMA = 1
CLI_API = 1
FRAMEWORK = f"maelys_cli python {sys.version_info.major}.{sys.version_info.minor}"

EXIT_OK = 0
EXIT_FAILURE = 1
EXIT_VIOLATIONS = 2

EXIT_CODES = {"0": "command completed", "1": "execution failed", "2": "valid report with violations"}
STABLE_CODES = ("INVALID_COMMAND", "VALIDATION_FAILED", "PRECONDITION_FAILED", "POLICY_FAILED", "ACCESS_DENIED",
                "NOT_FOUND", "IO_FAILED", "PROCESS_FAILED", "PROTOCOL_FAILED", "UNSUPPORTED", "UNEXPECTED")
IDENTIFIER = re.compile(r"^[a-z][a-z0-9.-]*$")
PREFIX_GRAMMAR = re.compile(r"^[a-z](?:[a-z0-9.-]*[a-z0-9-])?$")
FORMATS = ("text", "json", "jsonl")
COLORS = ("auto", "always", "never")
SHELLS = ("bash", "zsh", "fish")
RENDERING = ("--format", "--json", "--compact", "--pretty", "--color")
SIZE_UNITS = {"": 1, "K": 1024, "M": 1024 ** 2, "G": 1024 ** 3, "T": 1024 ** 4}
DURATION_UNITS = {"ms": 1, "s": 1000, "m": 60_000, "h": 3_600_000, "d": 86_400_000}

Handler = Callable[["Invocation"], "tuple[Any, int]"]


class Failure(Exception):
    """A failure envelope: stable code, causal message, next safe action."""

    def __init__(self, code: str, message: str, hint: str = "", issues: Optional[list] = None) -> None:
        super().__init__(message)
        self.code = code
        self.message = message
        self.hint = hint
        self.issues = issues


# ---- declarations ------------------------------------------------------------------

def argument(name: str, kind: str = "string", choices: Optional[list] = None, minimum: Optional[int] = None,
             maximum: Optional[int] = None, algorithms: Optional[list] = None, pattern: Optional[str] = None) -> dict:
    entry: dict = {"name": name, "type": kind}
    if choices is not None:
        entry["choices"] = list(choices)
    if minimum is not None:
        entry["minimum"] = minimum
    if maximum is not None:
        entry["maximum"] = maximum
    if algorithms is not None:
        entry["algorithms"] = list(algorithms)
    if pattern is not None:
        entry["pattern"] = pattern
    return entry


def option(long: str, summary: str, argument: Optional[dict] = None, default: Optional[str] = None,
           required: bool = False, repeatable: bool = False, requires: tuple = (), conflicts_with: tuple = (),
           group: Optional[str] = None) -> dict:
    """One option descriptor; `argument` from argument(), None for a flag."""
    if not long.startswith("--") or len(long) < 3:
        raise ValueError(f"an option is spelled --name, not {long!r}")
    entry: dict = {"long": long, "required": required, "repeatable": repeatable, "summary": summary,
                   "requires": list(requires), "conflictsWith": list(conflicts_with)}
    if argument is not None:
        entry["argument"] = argument
    if default is not None:
        entry["default"] = default
    if group is not None:
        entry["group"] = group
    return entry


def flag(long: str, summary: str, **keywords: Any) -> dict:
    return option(long, summary, None, **keywords)


def operand(name: str, summary: str, required: bool = True, variadic: bool = False, kind: Optional[str] = None,
            choices: Optional[list] = None, minimum: Optional[int] = None, maximum: Optional[int] = None) -> dict:
    entry: dict = {"name": name, "required": required, "variadic": variadic, "summary": summary}
    if kind is not None or choices is not None:
        entry["type"] = kind or "choice"
    if choices is not None:
        entry["choices"] = list(choices)
    if minimum is not None:
        entry["minimum"] = minimum
    if maximum is not None:
        entry["maximum"] = maximum
    return entry


def _command(identifier: str, pattern: str, purpose: str, handler: Optional[Handler], effect: Any,
             operands: tuple = (), options: tuple = (), schema: Optional[dict] = None, mode: str = "json-envelope",
             protocol: Optional[str] = None, external: bool = False, hidden: bool = False,
             unavailable: Optional[str] = None, passthrough: bool = False) -> dict:
    if not IDENTIFIER.match(identifier):
        raise ValueError(f"a command identifier is [a-z][a-z0-9.-]*, not {identifier!r}")
    words = pattern.split()
    operands = list(operands)
    options = list(options)
    variadic = [item for item in operands if item["variadic"]]
    if len(variadic) > 1 or (variadic and operands[-1] is not variadic[0]):
        raise ValueError(f"{identifier}: at most one operand is variadic, and it is the last one")
    synopsis = pattern
    for item in operands:
        piece = item["name"] + ("..." if item["variadic"] else "")
        synopsis += " " + (piece if item["required"] else f"[{piece}]")
    for item in options:
        piece = item["long"] + (f" {item['argument']['name']}" if "argument" in item else "")
        synopsis += " " + (piece if item["required"] else f"[{piece}]")
    if passthrough:
        synopsis += " [ARGUMENTS...]"
    return {"id": identifier, "pattern": words, "usage": synopsis, "purpose": purpose, "effect": effect,
            "outputMode": mode, "protocol": protocol, "external": external, "hidden": hidden,
            "unavailable": unavailable, "operands": operands, "options": options, "passthrough": passthrough,
            "outputSchema": schema or {"type": "object"}, "handler": handler}


def read(identifier: str, pattern: str, purpose: str, handler: Handler, **keywords: Any) -> dict:
    """Inspects or validates state; exit 2 reports violations in data."""
    return _command(identifier, pattern, purpose, handler, "read", **keywords)


def records(identifier: str, pattern: str, purpose: str, handler: Handler, **keywords: Any) -> dict:
    """A read whose data is {"count": N, "records": [...]}; accepts --format jsonl."""
    return _command(identifier, pattern, purpose, handler, "read", mode="json-records", **keywords)


def transaction(identifier: str, pattern: str, purpose: str, handler: Handler, commit: bool = False,
                options: tuple = (), **keywords: Any) -> dict:
    """Plans by default, writes with --apply; the handler reads invocation.apply."""
    effect = {"plan": "preview", "apply": "commit" if commit else "apply"}
    options = list(options) + [flag("--apply", "Apply the reviewed plan instead of only planning it.")]
    return _command(identifier, pattern, purpose, handler, effect, options=options, **keywords)


def execute(identifier: str, pattern: str, purpose: str, handler: Handler, **keywords: Any) -> dict:
    """Deliberately runs a non-transactional action."""
    return _command(identifier, pattern, purpose, handler, "execute", **keywords)


def stream(identifier: str, pattern: str, purpose: str, handler: Handler, protocol: Optional[str] = None,
           **keywords: Any) -> dict:
    """Owns stdio for a protocol; refuses rendering options; the handler returns the exit status."""
    return _command(identifier, pattern, purpose, handler, "stream", mode="protocol-stream", protocol=protocol,
                    **keywords)


def external(identifier: str, pattern: str, purpose: str, handler: Handler, **keywords: Any) -> dict:
    """Hands over to another executable; every word after the pattern reaches it verbatim."""
    return _command(identifier, pattern, purpose, handler, "execute", mode="protocol-stream", external=True,
                    passthrough=True, **keywords)


GLOBAL_OPTIONS = [
    option("--format", "Select text for humans, json for one envelope or jsonl for records.",
           argument("VALUE", "choice", FORMATS), default="text"),
    flag("--json", "Exact alias of --format json."),
    flag("--compact", "Render JSON on a single line."),
    flag("--pretty", "--pretty=false selects compact JSON."),
    flag("--non-interactive", "Never prompt; fail instead of asking a question."),
    option("--color", "Control ANSI colors on terminals.", argument("VALUE", "choice", COLORS), default="auto"),
    flag("--help", "Show the help of the selected command."),
]
INVARIANTS = [
    "usage and agent discovery share one catalog",
    "transactional commands plan by default and require --apply",
    "stdout carries success data; stderr carries failures",
    "--json and --format json are identical",
    "unknown or duplicated options are refused",
]


# ---- values ---------------------------------------------------------------------------

def parse_value(kind: str, text: str, spec: dict, where: str, usage: str) -> Any:
    """The typed value of one argument or operand, or a VALIDATION_FAILED."""
    def refuse(what: str) -> Failure:
        return Failure("VALIDATION_FAILED", f"{where} takes {what}, not '{text}'.", f"Use '{usage}'.")
    if kind == "boolean":
        if text not in ("true", "false"):
            raise refuse("true or false")
        return text == "true"
    if kind == "string":
        if "pattern" in spec and not re.match(spec["pattern"], text):
            raise refuse(f"a value matching {spec['pattern']}")
        return text
    if kind in ("integer", "unsigned"):
        if not re.fullmatch(r"-?\d+" if kind == "integer" else r"\d+", text):
            raise refuse("an integer" if kind == "integer" else "an unsigned integer")
        value = int(text)
    elif kind == "size":
        match = re.fullmatch(r"(\d+)([KMGT]?)", text)
        if not match:
            raise refuse("a size such as 512, 4K, 16M, 2G or 1T")
        value = int(match.group(1)) * SIZE_UNITS[match.group(2)]
    elif kind == "duration":
        match = re.fullmatch(r"(\d+)(ms|s|m|h|d)", text)
        if not match:
            raise refuse("a duration with its unit: ms, s, m, h or d")
        value = int(match.group(1)) * DURATION_UNITS[match.group(2)]
    elif kind == "path":
        if not text:
            raise refuse("a non-empty path")
        return text
    elif kind == "absolute-path":
        if not text.startswith("/"):
            raise refuse("an absolute path")
        return text
    elif kind == "choice":
        if text not in spec.get("choices", []):
            raise refuse(", ".join(spec.get("choices", [])))
        return text
    elif kind == "hex":
        if not re.fullmatch(r"[0-9a-f]+", text) or ("minimum" in spec and len(text) < spec["minimum"]) \
                or ("maximum" in spec and len(text) > spec["maximum"]):
            raise refuse("lowercase hexadecimal" + (f" of {spec['minimum']} characters" if spec.get("minimum") == spec.get("maximum") and "minimum" in spec else ""))
        return text
    elif kind == "sha256":
        if not re.fullmatch(r"[0-9a-f]{64}", text):
            raise refuse("a SHA-256 digest of 64 lowercase hexadecimal characters")
        return text
    elif kind == "digest":
        match = re.fullmatch(r"([a-z0-9-]+):([0-9a-f]+)", text)
        if not match or (spec.get("algorithms") and match.group(1) not in spec["algorithms"]):
            raise refuse("ALGORITHM:HEX with " + ", ".join(spec.get("algorithms", ["a declared algorithm"])))
        return text
    else:
        raise Failure("UNEXPECTED", f"Value kind {kind!r} of {where} is not known.", "Fix the catalog.")
    if ("minimum" in spec and value < spec["minimum"]) or ("maximum" in spec and value > spec["maximum"]):
        bounds = f"between {spec.get('minimum', '-inf')} and {spec.get('maximum', '+inf')}"
        raise Failure("VALIDATION_FAILED", f"{where} must be {bounds}, not {text}.", f"Use '{usage}'.")
    return value


# ---- the program ------------------------------------------------------------------

class Invocation:
    """What a handler receives: the resolved command and its validated input."""

    def __init__(self, program: "Program", command: dict, operands: list, options: dict,
                 fmt: str, compact: bool, non_interactive: bool, raw_operands: list) -> None:
        self.program = program
        self.command = command
        self.operands = operands
        self.raw_operands = raw_operands
        self.options = options
        self.format = fmt
        self.compact = compact
        self.non_interactive = non_interactive

    def option(self, name: str, default: Any = None) -> Any:
        """The typed value of an option, its declared default, or `default`."""
        if name in self.options:
            return self.options[name]
        definition = next((item for item in self.command["options"] if item["long"] == name), None)
        if definition and "default" in definition and "argument" in definition:
            return parse_value(definition["argument"]["type"], definition["default"], definition["argument"],
                               f"Option {name}", self.command["usage"])
        return default

    def flag(self, name: str) -> bool:
        return bool(self.options.get(name, False))

    @property
    def apply(self) -> bool:
        return self.flag("--apply")


class Program:
    def __init__(self, program: str, product: str, version: str, commands: list, guide: str = "",
                 text: Optional[dict] = None, framework: str = FRAMEWORK) -> None:
        self.program = program
        self.product = product
        self.version = version
        self.framework = framework
        self.guide_line = guide or product
        self.text = dict(text or {})
        self.catalog = [
            read("help", "help", "Show the help of the program or of one command.", self._help,
                 operands=[operand("COMMAND_ID", "Command identifier.", required=False)],
                 schema={"type": "object", "required": ["text", "commands"],
                         "properties": {"text": {"type": "string"},
                                        "commands": {"type": "array", "items": {"type": "string"}}}}),
            read("version", "version", "Show the product, program, version and contract.", self._version,
                 schema={"type": "object", "required": ["product", "program", "version", "contract", "cliApi", "framework"]}),
            read("describe", "describe", "Show the machine-readable catalog, or one descriptor.", self._describe,
                 operands=[operand("COMMAND_ID", "Command identifier.", required=False)],
                 options=[flag("--summary", "Every descriptor without its schema and exit codes."),
                          option("--prefix", "Select the command namespace PREFIX.",
                                 argument("PREFIX", "string", pattern=PREFIX_GRAMMAR.pattern),
                                 requires=("--summary",), conflicts_with=("COMMAND_ID",))],
                 schema={"type": "object", "required": ["schemaVersion", "kind", "program", "commands"]}),
            read("completion", "completion", "Print the shell completion script generated from the catalog.",
                 self._completion, operands=[operand("SHELL", "Shell.", choices=list(SHELLS))],
                 schema={"type": "object", "required": ["shell", "script"]}),
            records("complete.candidates", "__complete", "Completion candidates for the words given after --.",
                    self._complete, operands=[operand("WORDS", "Words typed so far.", required=False, variadic=True)],
                    hidden=True, schema={"type": "object", "required": ["count", "records"]}),
        ]
        seen = set()
        for command in commands:
            if command["id"] in seen or any(command["id"] == built["id"] for built in self.catalog):
                raise ValueError(f"duplicate command identifier {command['id']!r}")
            seen.add(command["id"])
            self._check_references(command)
            self.catalog.append(command)
        # usage in help; describe uses the catalog's synopsis
        self.catalog[0]["usage"] = "help [COMMAND_ID] | --help"
        self.catalog[1]["usage"] = "version | --version"

    def _check_references(self, command: dict) -> None:
        names = {item["long"] for item in command["options"]} | {item["long"] for item in GLOBAL_OPTIONS}
        operands = {item["name"] for item in command["operands"]}
        for item in command["options"]:
            for reference in item["requires"]:
                if reference not in names:
                    raise ValueError(f"{command['id']}: {item['long']} requires unknown option {reference}")
            for reference in item["conflictsWith"]:
                if not (reference in names or (not reference.startswith("--") and reference in operands)):
                    raise ValueError(f"{command['id']}: {item['long']} conflicts with unknown {reference}")

    # ---- catalog views ----

    def descriptor(self, command: dict, summary: bool = False) -> dict:
        entry: dict = {"id": command["id"], "pattern": command["pattern"], "usage": command["usage"],
                       "purpose": command["purpose"], "effect": command["effect"], "outputMode": command["outputMode"]}
        if command["protocol"]:
            entry["protocol"] = command["protocol"]
        entry.update({"external": command["external"], "hidden": command["hidden"],
                      "available": command["unavailable"] is None})
        if command["unavailable"] is not None:
            entry["unavailableReason"] = command["unavailable"]
        entry["input"] = {"synopsis": command["usage"], "operands": command["operands"],
                          "options": command["options"], "constraints": self.constraints(command),
                          "passthrough": command["passthrough"]}
        if not summary:
            entry["outputSchema"] = command["outputSchema"]
            entry["exitCodes"] = dict(EXIT_CODES)
        return entry

    @staticmethod
    def constraints(command: dict) -> list:
        entries = []
        groups: dict = {}
        for item in command["options"]:
            for other in item["requires"]:
                entries.append({"kind": "requires", "options": [item["long"], other]})
            for other in item["conflictsWith"]:
                if other.startswith("--"):
                    entries.append({"kind": "at-most-one", "options": [item["long"], other]})
            if "group" in item:
                groups.setdefault(item["group"], []).append(item["long"])
        entries.extend({"kind": "all-or-none", "options": members} for members in groups.values())
        return entries

    def command_by_id(self, identifier: str) -> dict:
        for command in self.catalog:
            if command["id"] == identifier:
                return command
        raise Failure("INVALID_COMMAND", f"Unknown command identifier '{identifier}'.",
                      "Run describe --summary to list the command identifiers.")

    def resolve(self, words: list) -> "tuple[Optional[dict], int]":
        best = None
        for command in self.catalog:
            pattern = command["pattern"]
            if words[:len(pattern)] == pattern and (best is None or len(pattern) > len(best["pattern"])):
                best = command
        return best, len(best["pattern"]) if best else 0

    def command_help(self, command: dict) -> str:
        lines = [f"{self.program} {command['usage']}", "", command["purpose"], ""]
        if command["operands"]:
            lines.append("OPERANDS")
            lines.extend(f"  {item['name']:<18} {item['summary']}" for item in command["operands"])
            lines.append("")
        if command["options"]:
            lines.append("OPTIONS")
            for item in command["options"]:
                spelled = item["long"] + (f" {item['argument']['name']}" if "argument" in item else "")
                default = f" (default {item['default']})" if "default" in item else ""
                lines.append(f"  {spelled:<28} {item['summary']}{default}")
            lines.append("")
        return "\n".join(lines).rstrip("\n") + "\n"

    def guide(self) -> str:
        visible = [command for command in self.catalog if not command["hidden"]]
        lines = [f"{self.program} {self.version} - {self.guide_line}", "", "USAGE",
                 f"  {self.program} COMMAND [OPERANDS] [OPTIONS]", "", "COMMANDS"]
        width = max(len(command["usage"]) for command in visible)
        lines.extend(f"  {command['usage']:<{width}}  {command['purpose']}" for command in visible)
        lines.extend(["", "GLOBAL OPTIONS"])
        for item in GLOBAL_OPTIONS:
            spelled = item["long"] + (f" {'|'.join(item['argument']['choices'])}" if "argument" in item else "")
            lines.append(f"  {spelled:<28} {item['summary']}")
        lines.extend(["", f"Run '{self.program} describe --format json' for the machine-readable catalog."])
        return "\n".join(lines) + "\n"

    # ---- built-in handlers ----

    def _version(self, invocation: Invocation) -> "tuple[dict, int]":
        return {"product": self.product, "program": self.program, "version": self.version, "contract": CONTRACT,
                "cliApi": CLI_API, "framework": self.framework}, EXIT_OK

    def _help(self, invocation: Invocation) -> "tuple[dict, int]":
        if invocation.operands:
            command = self.command_by_id(invocation.operands[0])
            return {"text": self.command_help(command), "commands": [command["id"]]}, EXIT_OK
        return {"text": self.guide(), "commands": [c["id"] for c in self.catalog if not c["hidden"]]}, EXIT_OK

    def _describe(self, invocation: Invocation) -> "tuple[dict, int]":
        data: dict = {"schemaVersion": CATALOG_SCHEMA, "program": self.program, "product": self.product,
                      "version": self.version, "contract": CONTRACT, "cliApi": CLI_API, "framework": self.framework}
        prefix = invocation.option("--prefix")
        if prefix is not None:
            selected = [self.descriptor(c, summary=True) for c in self.catalog
                        if c["id"] == prefix or c["id"].startswith(f"{prefix}.")]
            if not selected:
                raise Failure("INVALID_COMMAND", f"No command identifier equals or starts with '{prefix}.'.",
                              "Run describe --summary to list the command identifiers.")
            data.update({"kind": "summary", "filter": {"kind": "command-prefix", "value": prefix},
                         "commands": selected})
            return data, EXIT_OK
        if invocation.operands:
            data["kind"] = "command"
            data["commands"] = [self.descriptor(self.command_by_id(invocation.operands[0]))]
        elif invocation.flag("--summary"):
            data["kind"] = "summary"
            data["commands"] = [self.descriptor(c, summary=True) for c in self.catalog]
        else:
            data["kind"] = "catalog"
            data["globalOptions"] = GLOBAL_OPTIONS
            data["invariants"] = INVARIANTS
            data["output"] = {"contract": CONTRACT, "schemaVersion": SCHEMA_VERSION,
                              "stdout": "success data only; protocol streams are explicit exceptions",
                              "stderr": "diagnostics and failure envelopes"}
            data["commands"] = [self.descriptor(c) for c in self.catalog]
        return data, EXIT_OK

    def _completion(self, invocation: Invocation) -> "tuple[dict, int]":
        shell = invocation.operands[0]
        program = self.program
        function = f"_{program.replace('-', '_')}_complete"
        if shell == "bash":
            script = "\n".join([
                f"# bash completion for {program}, generated from its catalog",
                f"{function}() {{",
                "    local IFS=$'\\n'",
                '    local words=("${COMP_WORDS[@]:1:COMP_CWORD}")',
                f'    COMPREPLY=($("{program}" __complete -- "${{words[@]}}" 2>/dev/null))',
                "    if [ ${#COMPREPLY[@]} -eq 0 ]; then",
                '        COMPREPLY=($(compgen -f -- "${COMP_WORDS[COMP_CWORD]}"))',
                "    fi",
                "}",
                f"complete -o filenames -F {function} {program}",
            ]) + "\n"
        elif shell == "zsh":
            script = "\n".join([
                f"#compdef {program}",
                f"{function}() {{",
                "    local -a candidates",
                f'    candidates=("${{(@f)$("{program}" __complete -- "${{words[@]:1}}" 2>/dev/null)}}")',
                "    if (( ${#candidates} )); then compadd -- $candidates; else _files; fi",
                "}",
                f"compdef {function} {program}",
            ]) + "\n"
        else:
            script = "\n".join([
                f"# fish completion for {program}, generated from its catalog",
                f"complete -c {program} -f -a '({program} __complete -- (commandline -opc)[2..-1] (commandline -ct))'",
            ]) + "\n"
        return {"shell": shell, "script": script}, EXIT_OK

    def _complete(self, invocation: Invocation) -> "tuple[dict, int]":
        words = list(invocation.raw_operands)
        current = words[-1] if words else ""
        previous = words[:-1]
        candidates: list = []
        command, consumed = self.resolve(previous)
        for other in self.catalog:
            if other["hidden"] or other["unavailable"] is not None or len(other["pattern"]) <= len(previous):
                continue
            if other["pattern"][:len(previous)] == previous:
                candidates.append(other["pattern"][len(previous)])
        if command is not None and command["unavailable"] is None:
            given = {word.split("=", 1)[0] for word in previous[consumed:] if word.startswith("--")}
            if previous[consumed:] and previous[-1] == "--format":
                candidates = list(FORMATS)
            elif previous[consumed:] and previous[-1] == "--color":
                candidates = list(COLORS)
            else:
                if current.startswith("-") or not current:
                    candidates.extend(item["long"] for item in command["options"] + GLOBAL_OPTIONS
                                      if item["long"] not in given)
                position = len(previous) - consumed
                if command["id"] in ("help", "describe") and position == 0:
                    candidates.extend(other["id"] for other in self.catalog
                                      if not other["hidden"] and other["unavailable"] is None)
                elif position < len(command["operands"]) and command["operands"][position].get("choices"):
                    candidates.extend(command["operands"][position]["choices"])
        matching = sorted({word for word in candidates if word.startswith(current)})
        return {"count": len(matching), "records": [{"word": word} for word in matching]}, EXIT_OK

    # ---- parsing ----

    def parse(self, argv: list) -> "tuple[Invocation, dict]":
        """Validate the command line in the contract's causal order."""
        words: list = []
        raw: list = []
        passthrough: list = []
        index = 0
        command = None
        consumed = 0
        while index < len(argv):
            word = argv[index]
            command, consumed = self.resolve(words)
            if command is not None and command["passthrough"] and len(words) == consumed:
                passthrough = argv[index:]
                break
            if word == "--":
                passthrough = argv[index + 1:]
                break
            if word.startswith("--") and len(word) > 2:
                name, separator, value = word.partition("=")
                definition = next((item for item in GLOBAL_OPTIONS if item["long"] == name), None)
                if definition is None and command is not None:
                    definition = next((item for item in command["options"] if item["long"] == name), None)
                index += 1
                if definition and "argument" in definition and not separator:
                    if index >= len(argv):
                        raise Failure("VALIDATION_FAILED", f"Option {name} needs a value {definition['argument']['name']}.",
                                      "Run describe for this command and pass the option's argument.")
                    value, separator = argv[index], "="
                    index += 1
                raw.append((name, value if separator else None))
                continue
            if word.startswith("-") and word != "-":
                raise Failure("VALIDATION_FAILED", f"Option {word} is not supported: options are spelled --name.",
                              "Run describe for this command and use only its declared options.")
            words.append(word)
            index += 1
        if not words and any(name in ("--help", "--version") for name, _ in raw):
            words = ["help"] if any(name == "--help" for name, _ in raw) else ["version"]
            raw = [(name, value) for name, value in raw if name not in ("--help", "--version")]
        if not words:
            raise Failure("INVALID_COMMAND", "No command given.", f"Run '{self.program} help' or 'describe --summary'.")
        command, consumed = self.resolve(words)
        if command is None:
            raise Failure("INVALID_COMMAND", f"Unknown command '{' '.join(words)}'.",
                          "Run describe --summary and use one of the listed command identifiers.")
        if command["unavailable"] is not None:
            raise Failure("UNSUPPORTED", f"Command '{command['id']}' is not available in this build: {command['unavailable']}",
                          "Use another build of the product, or another command.")
        raw_operands = words[consumed:] + passthrough
        usage = command["usage"]
        fmt = os.environ.get("MAELYS_CLI_FORMAT", "text")
        compact = False
        non_interactive = False
        rendering: list = []
        help_requested = False
        options: dict = {}
        seen: set = set()
        known = {item["long"]: item for item in command["options"]}
        for name, value in raw:
            if name in seen and not (name in known and known[name]["repeatable"]):
                raise Failure("VALIDATION_FAILED", f"Option {name} is given twice.", "Give each option once.")
            seen.add(name)
            if name in RENDERING:
                rendering.append(name)
            if name == "--format":
                fmt = parse_value("choice", value or "", {"choices": list(FORMATS)}, "Option --format", usage)
            elif name == "--json":
                fmt = "json"
            elif name == "--compact":
                compact = value != "false"
            elif name == "--pretty":
                compact = value == "false"
            elif name == "--non-interactive":
                non_interactive = value != "false"
            elif name == "--color":
                parse_value("choice", value or "", {"choices": list(COLORS)}, "Option --color", usage)
            elif name == "--help":
                help_requested = True
            elif name in ("--dry-run", "--plan") and isinstance(command["effect"], dict):
                raise Failure("VALIDATION_FAILED", f"Option {name} is not supported by '{command['id']}': it plans by default.",
                              "Run without --apply to plan, then add --apply to write.")
            elif name not in known:
                raise Failure("VALIDATION_FAILED", f"Option {name} is not supported by '{command['id']}'. Use '{usage}'.",
                              "Run describe for this command and use only its declared options.")
            else:
                definition = known[name]
                if "argument" in definition:
                    if value is None:
                        raise Failure("VALIDATION_FAILED", f"Option {name} needs a value {definition['argument']['name']}.",
                                      "Pass the option's argument.")
                    typed = parse_value(definition["argument"]["type"], value, definition["argument"],
                                        f"Option {name}", usage)
                    if definition["repeatable"]:
                        options.setdefault(name, []).append(typed)
                    else:
                        options[name] = typed
                else:
                    options[name] = value != "false"
        # dependencies, conflicts, groups; then required; then operands
        for definition in command["options"]:
            name = definition["long"]
            if name not in options:
                continue
            for other in definition["requires"]:
                if other not in options and other not in seen:
                    raise Failure("VALIDATION_FAILED", f"Option {name} requires {other}.", f"Use '{usage}'.")
            for other in definition["conflictsWith"]:
                if other.startswith("--"):
                    if other in options or other in seen:
                        raise Failure("VALIDATION_FAILED", f"Option {name} conflicts with {other}.", f"Use '{usage}'.")
                else:
                    position = next((i for i, item in enumerate(command["operands"]) if item["name"] == other), None)
                    if position is not None and len(raw_operands) > position:
                        raise Failure("VALIDATION_FAILED", f"Option {name} conflicts with the {other} operand.",
                                      f"Use '{usage}'.")
        groups: dict = {}
        for definition in command["options"]:
            if "group" in definition:
                groups.setdefault(definition["group"], []).append(definition["long"])
        for members in groups.values():
            present = [name for name in members if name in options]
            if present and len(present) != len(members):
                raise Failure("VALIDATION_FAILED", f"Options {', '.join(members)} are given together or not at all.",
                              f"Use '{usage}'.")
        for definition in command["options"]:
            if definition["required"] and definition["long"] not in options and not help_requested:
                raise Failure("VALIDATION_FAILED", f"Option {definition['long']} is required by '{command['id']}'.",
                              f"Use '{usage}'.")
        if help_requested:
            help_command = self.command_by_id("help")
            return Invocation(self, help_command, [command["id"]], {}, fmt, compact, non_interactive,
                              [command["id"]]), help_command
        operands: list = []
        if not command["passthrough"]:
            required = sum(1 for item in command["operands"] if item["required"])
            variadic = any(item["variadic"] for item in command["operands"])
            if len(raw_operands) < required or (not variadic and len(raw_operands) > len(command["operands"])):
                raise Failure("VALIDATION_FAILED", f"Operands do not match '{command['id']}'. Use '{usage}'.",
                              "Use the synopsis returned by describe and retry.")
            for position, value in enumerate(raw_operands):
                item = command["operands"][min(position, len(command["operands"]) - 1)]
                kind = item.get("type")
                operands.append(parse_value(kind, value, item, item["name"], usage) if kind else value)
        else:
            operands = list(raw_operands)
        if command["outputMode"] == "protocol-stream" and rendering:
            raise Failure("VALIDATION_FAILED", f"Command '{command['id']}' owns its stdout and refuses {rendering[0]}.",
                          "Set MAELYS_CLI_FORMAT=json in the environment to receive its failure envelope as JSON.")
        if fmt == "jsonl" and command["outputMode"] != "json-records":
            raise Failure("VALIDATION_FAILED", f"--format jsonl is accepted only by json-records commands, not '{command['id']}'.",
                          "Use --format json.")
        return Invocation(self, command, operands, options, fmt, compact, non_interactive, raw_operands), command

    # ---- rendering ----

    @staticmethod
    def envelope(command_id: str, ok: bool, exit_code: int, payload: Any, compact: bool) -> str:
        body: dict = {"schemaVersion": SCHEMA_VERSION, "contract": CONTRACT, "command": command_id, "ok": ok,
                      "exitCode": exit_code}
        body["data" if ok else "error"] = payload
        return json.dumps(body, separators=(",", ":") if compact else None, indent=None if compact else 2,
                          ensure_ascii=False) + "\n"

    def render_text(self, command: dict, data: Any) -> str:
        renderer = self.text.get(command["id"])
        if renderer is not None:
            return renderer(data)
        if command["id"] == "help":
            return data["text"]
        if command["id"] == "completion":
            return data["script"]
        if command["id"] == "version":
            return f"{self.program} {self.version}\n"
        if command["outputMode"] == "json-records":
            return "".join(self._record_line(record) for record in data.get("records", []))
        return json.dumps(data, indent=2, ensure_ascii=False) + "\n"

    @staticmethod
    def _record_line(record: Any) -> str:
        if isinstance(record, dict) and len(record) == 1:
            return f"{next(iter(record.values()))}\n"
        return json.dumps(record, separators=(",", ":"), ensure_ascii=False) + "\n"

    @staticmethod
    def _colored(text: str, argv: list) -> str:
        colors = [word.split("=", 1)[1] for word in argv if word.startswith("--color=")]
        colors += [argv[i + 1] for i, word in enumerate(argv) if word == "--color" and i + 1 < len(argv)]
        never = "never" in colors or os.environ.get("NO_COLOR") or os.environ.get("TERM") == "dumb"
        if never or not ("always" in colors or sys.stderr.isatty()):
            return text
        return f"\033[31m{text}\033[0m"

    def main(self, argv: Optional[list] = None) -> int:
        argv = list(sys.argv[1:] if argv is None else argv)
        command_id = ""
        fmt = os.environ.get("MAELYS_CLI_FORMAT", "text")
        for index, word in enumerate(argv):
            if word in ("--json", "--format=json", "--format=jsonl"):
                fmt = "json"
            elif word == "--format" and index + 1 < len(argv) and argv[index + 1] in ("json", "jsonl"):
                fmt = "json"
        compact = "--compact" in argv or "--pretty=false" in argv
        try:
            invocation, command = self.parse(argv)
            command_id, fmt, compact = command["id"], invocation.format, invocation.compact
            if command["outputMode"] == "protocol-stream":
                result = command["handler"](invocation)
                return int(result[1] if isinstance(result, tuple) else result)
            data, exit_code = command["handler"](invocation)
            if fmt == "text":
                sys.stdout.write(self.render_text(command, data))
            elif fmt == "jsonl":
                for record in data.get("records", []):
                    sys.stdout.write(json.dumps(record, separators=(",", ":"), ensure_ascii=False) + "\n")
            else:
                sys.stdout.write(self.envelope(command_id, True, exit_code, data, compact))
            sys.stdout.flush()
            return exit_code
        except Failure as failure:
            error: dict = {"code": failure.code, "message": failure.message}
            if failure.hint:
                error["hint"] = failure.hint
            if failure.issues:
                error["issues"] = failure.issues
            if fmt == "text":
                sys.stderr.write(self._colored(f"{self.program}: [{failure.code}] {failure.message}", argv) + "\n")
                if failure.hint:
                    sys.stderr.write(f"Hint: {failure.hint}\n")
            else:
                sys.stderr.write(self.envelope(command_id or "unknown", False, EXIT_FAILURE, error, compact))
            return EXIT_FAILURE
        except OSError as error:
            message = f"{error.strerror}: {error.filename}" if error.filename else str(error)
            payload = {"code": "IO_FAILED", "message": message, "hint": "Check the path and its permissions."}
            if fmt == "text":
                sys.stderr.write(f"{self.program}: [IO_FAILED] {message}\nHint: {payload['hint']}\n")
            else:
                sys.stderr.write(self.envelope(command_id or "unknown", False, EXIT_FAILURE, payload, compact))
            return EXIT_FAILURE
