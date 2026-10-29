// SPDX-License-Identifier: MPL-2.0
import { Failure, parseFlag, parseValue, refusal, type Json } from "./values.js";
import { Invocation, type Program } from "./runtime.js";
import type { Command, Option } from "./catalog.js";
export const environmentFormat = () => process.env.MAELYS_CLI_FORMAT === "json" ? "json" : "text";
// The index of the first word that is neither a global option nor the value it takes, as
// maelys_cli_leading_globals does in C: --help is no global option here, being the word that selects the help.
export function leadingGlobals(program: Program, argv: string[]): number {
  let index = 0;
  while (index < argv.length && argv[index].startsWith("--") && argv[index].length > 2) {
    const equals = argv[index].indexOf("=");
    const name = equals < 0 ? argv[index] : argv[index].slice(0, equals);
    const definition = program.globalOptions.find(o => o.long === name);
    if (!definition || name === "--help") break;
    if (definition.argument && equals < 0) {
      if (index + 1 >= argv.length) break;
      index++;
    }
    index++;
  }
  return index;
}
// The issue of an operand count the command refuses: the first required operand the line lacks, or the
// operands as a whole -- never a word the line gave.
function arityIssue(definitions: Command["input"]["operands"], given: number): [string, string] {
  const absent = definitions.slice(given).find(o => o.required);
  return given < definitions.filter(o => o.required).length && absent ? ["MISSING_OPERAND", absent.name] : ["EXTRA_OPERAND", "operands"];
}
export function parse(program: Program, argv: string[]): Invocation {
  let words: string[] = [], raw: [string, string | null | symbol][] = [], tail: string[] = [];
  const dash = Symbol(), bare = Symbol();
  const fallback = program.defaultCommand ? program.commandById(program.defaultCommand) : undefined;
  let index = 0;
  // A global option selects no command: after global options only, --help and --version select their command
  // as in first place, the options being that command's (agent-cli/v2 2.22.0, section 6).
  const at = leadingGlobals(program, argv);
  if (at > 0 && at < argv.length && ["--help", "--version"].includes(argv[at])) argv = [argv[at], ...argv.slice(0, at), ...argv.slice(at + 1)];
  if (argv.length && ["--help", "--version"].includes(argv[0])) {
    // As the first word, each is its command spelled as an option, the rest of the line its own:
    // `--version --help` is the help of version, `--help --version` an option help lacks.
    words.push(argv[0] === "--help" ? "help" : "version");
    index = 1;
  }
  const implicit = fallback && (!argv.length || argv[0].startsWith("-") && !["--help", "--version"].includes(argv[0]));
  // The command is resolved from the words that start the line, before any option, as in C: bare words after
  // an option are operands of the command those leading words name.
  let leading: number | undefined;
  while (index < argv.length) {
    const word = argv[index];
    const [current, consumed] = implicit ? [fallback, 0] : program.resolve(leading === undefined ? words : words.slice(0, leading));
    if (word.startsWith("-") && word !== "-" && word !== "--" && leading === undefined) leading = words.length;
    if (current?.input.passthrough && words.length === consumed) { tail = argv.slice(index); break; }
    if (word === "--") { tail = argv.slice(index + 1); break; }
    if (word.startsWith("--") && word.length > 2) {
      const equals = word.indexOf("=");
      const name = equals < 0 ? word : word.slice(0, equals);
      let value = equals < 0 ? null : word.slice(equals + 1);
      const definition = program.globalOptions.find(o => o.long === name) ?? current?.input.options.find(o => o.long === name);
      index++;
      if (definition?.argument && equals < 0 && index < argv.length) value = argv[index++];
      raw.push([name, value]); continue;
    }
    if (word.startsWith("-") && word !== "-") raw.push([word, dash]);
    else if (implicit) raw.push([word, bare]);
    else words.push(word);
    index++;
  }
  leading ??= words.length;
  if (implicit) words = [...fallback.pattern], leading = words.length;
  // Without a default command, an empty line is the help, as in C.
  else if (!argv.length) words = ["help"], leading = 1;
  // A first word that starts with a dash, --help and --version apart, names no command: options come after
  // the command's pattern, and a word with one dash selects nothing either (`-x --help`), as in C.
  else if (leading === 0) throw refusal("INVALID_COMMAND", "Unknown command '" + argv[0] + "'.", "Run describe --summary and use one of the listed command identifiers.", "UNKNOWN_COMMAND", argv[0]);
  let [command, consumed] = program.resolve(words.slice(0, leading));
  if (!command && raw.some(([name]) => name === "--help")) {
    const family = program.family(undefined, words.slice(0, leading));
    if (family.length) {
      program.pendingFamily = words.slice(0, leading);
      tail = [];
      words = ["help", ...words.slice(leading)];
      leading = 1;
      raw = raw.filter(([name]) => name !== "--help");
      [command, consumed] = program.resolve(words.slice(0, 1));
    }
  }
  if (!command) throw refusal("INVALID_COMMAND", "Unknown command '" + words.slice(0, leading).join(" ") + "'.", "Run describe --summary and use one of the listed command identifiers.", "UNKNOWN_COMMAND", argv[0] ?? "");
  program.resolvedCommand = command.id;
  const operands = [...words.slice(consumed), ...tail], usage = command.usage;
  const invocation = new Invocation(program, command);
  invocation.format = environmentFormat();
  const known = new Map(command.input.options.map(o => [o.long, o]));
  const seen = new Set<string>();
  const rendering: string[] = [];
  let help = false, formatRequested = false;
  for (const [name, rawValue] of raw) {
    if (seen.has(name) && !known.get(name)?.repeatable) throw refusal("VALIDATION_FAILED", "Option " + name + " is given twice.", "Give each option once.", "DUPLICATE_OPTION", name);
    if (rawValue === dash) throw refusal("VALIDATION_FAILED", "Option " + name + " is not supported: options are spelled --name.", "Spell an option --name; write an operand that starts with a dash after --.", "NOT_AN_OPTION", name);
    if (rawValue === bare) throw refusal("VALIDATION_FAILED", "'" + name + "' is neither an option nor the value of one: the line names no command and runs the default command '" + command.id + "', whose operands are written after --.", undefined, "UNEXPECTED_OPERAND", "operands");
    const value = rawValue as string | null;
    seen.add(name);
    const declared = known.get(name) ?? program.globalOptions.find(o => o.long === name);
    if (declared?.argument && value === null) throw refusal("VALIDATION_FAILED", "Option " + name + " needs a value " + declared.argument.name + ".", "Run describe for this command and pass the option's argument.", "MISSING_VALUE", name);
    if (["--format", "--json", "--compact", "--pretty", "--pager", "--field"].includes(name)) rendering.push(name);
    if (name === "--format") { invocation.format = String(parseValue(declared!.argument!, value!, "Option " + name, usage)); formatRequested = true; }
    else if (name === "--json") { invocation.format = parseFlag(value, name) ? "json" : "text"; formatRequested = true; }
    else if (name === "--compact") invocation.compact = parseFlag(value, name);
    else if (name === "--pretty") invocation.compact = !parseFlag(value, name);
    else if (name === "--non-interactive") invocation.nonInteractive = parseFlag(value, name);
    else if (name === "--verbose") invocation.verbose = parseFlag(value, name);
    else if (name === "--color" || name === "--pager" || name === "--progress") {
      const parsed = String(parseValue(declared!.argument!, value!, "Option " + name, usage));
      if (name === "--color") invocation.color = parsed;
      else if (name === "--pager") invocation.pager = parsed;
      else invocation.progress = parsed;
    } else if (name === "--field") invocation.field = value!;
    else if (name === "--lang") invocation.lang = String(parseValue(declared!.argument!, value!, "Option " + name, usage));
    else if (name === "--help") help = parseFlag(value, name);
    else if (["--dry-run", "--plan"].includes(name) && typeof command.effect === "object") throw refusal("VALIDATION_FAILED", "Option " + name + " is not supported by '" + command.id + "': it plans by default.", "Run without --apply to plan, then add --apply to write.", "UNKNOWN_OPTION", name);
    else if (!known.has(name)) throw refusal("VALIDATION_FAILED", "Option " + name + " is not supported by '" + command.id + "'. Use '" + usage + "'.", "Run describe for this command and use only its declared options.", "UNKNOWN_OPTION", name);
    else {
      const definition = known.get(name)!;
      const typed = definition.argument ? parseValue(definition.argument, value!, "Option " + name, usage) : parseFlag(value, name);
      if (definition.repeatable) invocation.options.set(name, [...(invocation.options.get(name) as Json[] ?? []), typed]);
      else invocation.options.set(name, typed);
    }
  }
  if (help) {
    command = program.commandById("help");
    program.resolvedCommand = "help";
    invocation.command = command;
    invocation.operands = [words.length ? program.resolve(words.slice(0, leading))[0]!.id : "help"];
    invocation.rawOperands = invocation.operands.map(String);
    if (invocation.format === "jsonl" && invocation.field === undefined) throw refusal("VALIDATION_FAILED", "--format jsonl is accepted only by json-records commands, not 'help'; add --field to render one member in jsonl.", "Use --format json.", "RENDERING", "--format");
    return invocation;
  }
  const enabled = (name: string) => invocation.options.has(name) && invocation.options.get(name) !== false;
  for (const definition of command.input.options) {
    if (!enabled(definition.long)) continue;
    for (const other of definition.requires) if (!enabled(other)) throw refusal("VALIDATION_FAILED", "Option " + definition.long + " requires " + other + ".", "Use '" + usage + "'.", "REQUIRES", definition.long);
    for (const other of definition.conflictsWith) {
      if (other.startsWith("--") && enabled(other)) throw refusal("VALIDATION_FAILED", "Option " + definition.long + " conflicts with " + other + ".", undefined, "CONFLICTS", definition.long);
      if (!other.startsWith("--") && operands.length > command.input.operands.findIndex(o => o.name === other)) throw refusal("VALIDATION_FAILED", "Option " + definition.long + " conflicts with the " + other + " operand.", undefined, "CONFLICTS", definition.long);
    }
  }
  for (const rule of command.input.constraints) {
    const selected = rule.options.filter(enabled);
    if (rule.kind === "requires" && enabled(rule.options[0]) && rule.options.slice(1).some(n => !enabled(n))) throw refusal("VALIDATION_FAILED", "Option " + rule.options[0] + " requires " + rule.options.slice(1).find(n => !enabled(n)) + ".", undefined, "REQUIRES", rule.options[0]);
    if (rule.kind === "at-most-one" && selected.length > 1) throw refusal("VALIDATION_FAILED", "At most one of " + rule.options.join(", ") + " may be given.", undefined, "AT_MOST_ONE", rule.options.join(","));
    if (rule.kind === "exactly-one" && selected.length !== 1) throw refusal("VALIDATION_FAILED", "Exactly one of " + rule.options.join(", ") + " must be given.", undefined, "EXACTLY_ONE", rule.options.join(","));
    if (rule.kind === "all-or-none" && selected.length > 0 && selected.length !== rule.options.length) throw refusal("VALIDATION_FAILED", "Options " + rule.options.join(", ") + " are given together or not at all.", undefined, "ALL_OR_NONE", rule.options.join(","));
  }
  for (const definition of command.input.options) if (definition.required && !invocation.options.has(definition.long)) throw refusal("VALIDATION_FAILED", "Option " + definition.long + " is required by '" + command.id + "'.", "Use '" + usage + "'.", "REQUIRED_OPTION", definition.long);
  invocation.rawOperands = operands;
  if (command.input.passthrough) invocation.operands = operands;
  else {
    const definitions = command.input.operands;
    if (operands.length < definitions.filter(o => o.required).length || !definitions.some(o => o.variadic) && operands.length > definitions.length) throw refusal("VALIDATION_FAILED", "Operands do not match '" + command.id + "'. Use '" + usage + "'.", "Use the synopsis returned by describe and retry.", ...arityIssue(definitions, operands.length));
    invocation.operands = operands.map((value, i) => {
      const spec = definitions[Math.min(i, definitions.length - 1)];
      return parseValue(spec, value, spec.name, usage);
    });
  }
  if (!command.available) throw new Failure(command.unavailableCode ?? "UNSUPPORTED", "Command '" + command.id + "' is not available in this build: " + command.unavailableReason, "Use another build of the product, or another command.");
  if (command.outputMode === "protocol-stream" && rendering.length) throw refusal("VALIDATION_FAILED", "Command '" + command.id + "' owns its stdout and refuses " + rendering[0] + ".", "Set MAELYS_CLI_FORMAT=json in the environment to receive its failure envelope as JSON.", "RENDERING", rendering[0]);
  if (invocation.format === "jsonl" && invocation.field === undefined && command.outputMode !== "json-records") throw refusal("VALIDATION_FAILED", "--format jsonl is accepted only by json-records commands, not '" + command.id + "'; add --field to render one member in jsonl.", "Use --format json.", "RENDERING", "--format");
  if (invocation.field !== undefined && invocation.format === "json" && formatRequested) throw refusal("VALIDATION_FAILED", "--field conflicts with --format json: a filtered envelope would not validate against the command's outputSchema.", "Use --format text or --format jsonl with --field.", "RENDERING", "--field");
  return invocation;
}
