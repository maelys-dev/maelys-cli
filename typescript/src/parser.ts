// SPDX-License-Identifier: MPL-2.0
import { Failure, parseFlag, parseValue, type Json } from "./values.js";
import { Invocation, type Program } from "./runtime.js";
import type { Command, Option } from "./catalog.js";
export const environmentFormat = () => process.env.MAELYS_CLI_FORMAT === "json" ? "json" : "text";
export function parse(program: Program, argv: string[]): Invocation {
  let words: string[] = [], raw: [string, string | null | symbol][] = [], tail: string[] = [];
  const dash = Symbol(), bare = Symbol();
  const fallback = program.defaultCommand ? program.commandById(program.defaultCommand) : undefined;
  const implicit = fallback && (!argv.length || argv[0].startsWith("-") && !["--help", "--version"].includes(argv[0]));
  let index = 0;
  while (index < argv.length) {
    const word = argv[index];
    const [current, consumed] = implicit ? [fallback, 0] : program.resolve(words);
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
  if (implicit) words = [...fallback.pattern];
  else if (!words.length && raw.some(([name]) => ["--help", "--version"].includes(name)) && !raw.some(([, value]) => value === dash)) {
    words = [raw.some(([name]) => name === "--help") ? "help" : "version"];
    raw = raw.filter(([name]) => !["--help", "--version"].includes(name));
  }
  if (!words.length) throw new Failure("INVALID_COMMAND", "No command given.", "Run '" + program.program + " help' or 'describe --summary'.");
  let [command, consumed] = program.resolve(words);
  if (!command && raw.some(([name]) => name === "--help")) {
    const family = program.family(undefined, words);
    if (family.length) {
      program.pendingFamily = [...words];
      tail = [];
      words = ["help"];
      raw = raw.filter(([name]) => name !== "--help");
      [command, consumed] = program.resolve(words);
    }
  }
  if (!command) throw new Failure("INVALID_COMMAND", "Unknown command '" + words.join(" ") + "'.", "Run describe --summary and use one of the listed command identifiers.");
  program.resolvedCommand = command.id;
  const operands = [...words.slice(consumed), ...tail], usage = command.usage;
  const invocation = new Invocation(program, command);
  invocation.format = environmentFormat();
  const known = new Map(command.input.options.map(o => [o.long, o]));
  const seen = new Set<string>();
  const rendering: string[] = [];
  let help = false, formatRequested = false;
  for (const [name, rawValue] of raw) {
    if (seen.has(name) && !known.get(name)?.repeatable) throw new Failure("VALIDATION_FAILED", "Option " + name + " is given twice.", "Give each option once.");
    if (rawValue === dash) throw new Failure("VALIDATION_FAILED", "Option " + name + " is not supported: options are spelled --name.", "Spell an option --name; write an operand that starts with a dash after --.");
    if (rawValue === bare) throw new Failure("VALIDATION_FAILED", "'" + name + "' is neither an option nor the value of one: the line names no command and runs the default command '" + command.id + "', whose operands are written after --.");
    const value = rawValue as string | null;
    seen.add(name);
    const declared = known.get(name) ?? program.globalOptions.find(o => o.long === name);
    if (declared?.argument && value === null) throw new Failure("VALIDATION_FAILED", "Option " + name + " needs a value " + declared.argument.name + ".", "Run describe for this command and pass the option's argument.");
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
    else if (["--dry-run", "--plan"].includes(name) && typeof command.effect === "object") throw new Failure("VALIDATION_FAILED", "Option " + name + " is not supported by '" + command.id + "': it plans by default.", "Run without --apply to plan, then add --apply to write.");
    else if (!known.has(name)) throw new Failure("VALIDATION_FAILED", "Option " + name + " is not supported by '" + command.id + "'. Use '" + usage + "'.", "Run describe for this command and use only its declared options.");
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
    invocation.operands = [words.length ? program.resolve(words)[0]!.id : "help"];
    invocation.rawOperands = invocation.operands.map(String);
    if (invocation.format === "jsonl" && invocation.field === undefined) throw new Failure("VALIDATION_FAILED", "--format jsonl is accepted only by json-records commands, not 'help'; add --field to render one member in jsonl.", "Use --format json.");
    return invocation;
  }
  const enabled = (name: string) => invocation.options.has(name) && invocation.options.get(name) !== false;
  for (const definition of command.input.options) {
    if (!enabled(definition.long)) continue;
    for (const other of definition.requires) if (!enabled(other)) throw new Failure("VALIDATION_FAILED", "Option " + definition.long + " requires " + other + ".", "Use '" + usage + "'.");
    for (const other of definition.conflictsWith) {
      if (other.startsWith("--") && enabled(other)) throw new Failure("VALIDATION_FAILED", "Option " + definition.long + " conflicts with " + other + ".");
      if (!other.startsWith("--") && operands.length > command.input.operands.findIndex(o => o.name === other)) throw new Failure("VALIDATION_FAILED", "Option " + definition.long + " conflicts with the " + other + " operand.");
    }
  }
  for (const rule of command.input.constraints) {
    const selected = rule.options.filter(enabled);
    if (rule.kind === "requires" && enabled(rule.options[0]) && rule.options.slice(1).some(n => !enabled(n))) throw new Failure("VALIDATION_FAILED", "Option " + rule.options[0] + " requires " + rule.options.slice(1).find(n => !enabled(n)) + ".");
    if (rule.kind === "at-most-one" && selected.length > 1) throw new Failure("VALIDATION_FAILED", "At most one of " + rule.options.join(", ") + " may be given.");
    if (rule.kind === "exactly-one" && selected.length !== 1) throw new Failure("VALIDATION_FAILED", "Exactly one of " + rule.options.join(", ") + " must be given.");
    if (rule.kind === "all-or-none" && selected.length > 0 && selected.length !== rule.options.length) throw new Failure("VALIDATION_FAILED", "Options " + rule.options.join(", ") + " are given together or not at all.");
  }
  for (const definition of command.input.options) if (definition.required && !invocation.options.has(definition.long)) throw new Failure("VALIDATION_FAILED", "Option " + definition.long + " is required by '" + command.id + "'.", "Use '" + usage + "'.");
  invocation.rawOperands = operands;
  if (command.input.passthrough) invocation.operands = operands;
  else {
    const definitions = command.input.operands;
    if (operands.length < definitions.filter(o => o.required).length || !definitions.some(o => o.variadic) && operands.length > definitions.length) throw new Failure("VALIDATION_FAILED", "Operands do not match '" + command.id + "'. Use '" + usage + "'.", "Use the synopsis returned by describe and retry.");
    invocation.operands = operands.map((value, i) => {
      const spec = definitions[Math.min(i, definitions.length - 1)];
      return parseValue(spec, value, spec.name, usage);
    });
  }
  if (!command.available) throw new Failure(command.unavailableCode ?? "UNSUPPORTED", "Command '" + command.id + "' is not available in this build: " + command.unavailableReason, "Use another build of the product, or another command.");
  if (command.outputMode === "protocol-stream" && rendering.length) throw new Failure("VALIDATION_FAILED", "Command '" + command.id + "' owns its stdout and refuses " + rendering[0] + ".", "Set MAELYS_CLI_FORMAT=json in the environment to receive its failure envelope as JSON.");
  if (invocation.format === "jsonl" && invocation.field === undefined && command.outputMode !== "json-records") throw new Failure("VALIDATION_FAILED", "--format jsonl is accepted only by json-records commands, not '" + command.id + "'; add --field to render one member in jsonl.", "Use --format json.");
  if (invocation.field !== undefined && invocation.format === "json" && formatRequested) throw new Failure("VALIDATION_FAILED", "--field conflicts with --format json: a filtered envelope would not validate against the command's outputSchema.", "Use --format text or --format jsonl with --field.");
  return invocation;
}
