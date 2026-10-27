// SPDX-License-Identifier: MPL-2.0
import { argument, Failure, parseValue, validateValue, type Json, type Schema, type Value } from "./values.js";
import type { Invocation } from "./runtime.js";
export { argument };
export type Result = { data: Json; exitCode?: 0 | 2 };
export type Handler = (invocation: Invocation) => Result | number | Promise<Result | number>;
export type Option = { long: string; summary: string; argument?: Value; default?: string; required: boolean; repeatable: boolean; requires: string[]; conflictsWith: string[]; group?: string; hidden?: boolean };
export type Operand = Value & { summary: string; required: boolean; variadic: boolean };
export type Constraint = { kind: "requires" | "at-most-one" | "exactly-one" | "all-or-none"; options: string[] };
export type Example = { words: string[]; summary: string };
export type Effect = "read" | "execute" | "stream" | { plan: "preview"; apply: "apply" | "commit" };
export type Command = { id: string; pattern: string[]; usage: string; purpose: string; effect: Effect; outputMode: "json-envelope" | "json-records" | "protocol-stream"; input: { synopsis: string; operands: Operand[]; options: Option[]; constraints: Constraint[]; passthrough: boolean }; outputSchema: Schema; exitCodes: Record<string, string>; external: boolean; hidden: boolean; available: boolean; unavailableReason?: string; unavailableCode?: string; protocol?: string; examples?: Example[]; handler: Handler };
export type CommandOptions = { operands?: Operand[]; options?: Option[]; constraints?: Constraint[]; examples?: Example[]; schema?: Schema; synopsis?: string; hidden?: boolean; unavailable?: string; unavailableCode?: string; protocol?: string; passthrough?: boolean };
export const EXIT_CODES = { "0": "command completed", "1": "execution failed", "2": "valid report with violations" };
export function option(long: string, summary: string, value?: Value, options: Partial<Omit<Option, "long" | "summary" | "argument">> = {}): Option {
  if (!/^--[a-z][a-z0-9-]*$(?![\s\S])/.test(long)) throw new TypeError("Options are spelled --name.");
  if (options.hidden && options.required) throw new TypeError("A required option must be shown.");
  if (value) validateValue(value);
  const result: Option = { long, summary, required: false, repeatable: false, requires: [], conflictsWith: [], ...options, ...(value ? { argument: value } : {}) };
  if (result.default !== undefined && value) parseValue(value, result.default, long, "");
  return result;
}
export function flag(long: string, summary: string, options: Partial<Omit<Option, "long" | "summary" | "argument">> = {}): Option { return option(long, summary, undefined, options); }
export function operand(name: string, summary: string, options: Partial<Omit<Operand, "name" | "summary">> = {}): Operand {
  const result = { name, summary, required: true, variadic: false, ...options };
  validateValue(result); return result;
}
export function constraint(kind: Constraint["kind"], ...options: string[]): Constraint {
  if (kind === "all-or-none" || !["requires", "at-most-one", "exactly-one"].includes(kind) || options.length < 2 || new Set(options).size !== options.length) throw new TypeError("Invalid constraint; all-or-none is declared by option group.");
  return { kind, options };
}
export function example(words: string, summary: string): Example {
  if (!words || !summary || words.trim() !== words || words.includes("  ") || /[\x00-\x1f\x7f]/.test(words + summary)) throw new TypeError("Invalid command example.");
  return { words: words.split(" "), summary };
}
function make(id: string, pattern: string, purpose: string, handler: Handler, effect: Effect, mode: Command["outputMode"], opts: CommandOptions = {}, external = false): Command {
  if (!/^[a-z][a-z0-9-]*(\.[a-z0-9][a-z0-9-]*)*$(?![\s\S])/.test(id) || id === "unknown") throw new TypeError("Invalid command identifier.");
  if (!pattern || /[\x00-\x1f\x7f]/.test(pattern) || pattern.trim() !== pattern || pattern.includes("  ")) throw new TypeError("Invalid command pattern.");
  const operands = opts.operands ?? [], options = opts.options ?? [];
  if (operands.filter(v => v.variadic).length > 1 || operands.some((v, i) => v.variadic && i !== operands.length - 1) || operands.some((v, i) => v.required && operands.slice(0, i).some(p => !p.required))) throw new TypeError("Invalid operand order.");
  const names = options.map(o => o.long);
  if (new Set(names).size !== names.length) throw new TypeError("Duplicate option.");
  for (const opt of options) {
    for (const other of [...opt.requires, ...opt.conflictsWith]) if (!names.includes(other) && !operands.some(o => o.name === other)) throw new TypeError("Unknown option reference: " + other);
  }
  const constraints: Constraint[] = [];
  const groups = new Map<string, string[]>();
  for (const opt of options) {
    for (const other of opt.requires) constraints.push({ kind: "requires", options: [opt.long, other] });
    for (const other of opt.conflictsWith.filter(v => v.startsWith("--"))) constraints.push({ kind: "at-most-one", options: [opt.long, other] });
    if (opt.group) groups.set(opt.group, [...(groups.get(opt.group) ?? []), opt.long]);
  }
  for (const members of groups.values()) { if (members.length < 2) throw new TypeError("An option group needs two members."); constraints.push({ kind: "all-or-none", options: members }); }
  for (const rule of opts.constraints ?? []) {
    if (rule.options.some(n => !names.includes(n))) throw new TypeError("Constraint names an undeclared option.");
    constraints.push(rule);
  }
  if (opts.passthrough && !external) throw new TypeError("Only external commands accept passthrough.");
  const usage = opts.synopsis ?? [pattern, ...operands.map(o => {
    const name = o.name + (o.variadic ? "..." : "");
    return o.required ? name : "[" + name + "]";
  }), ...options.filter(o => !o.hidden).sort((a, b) => Number(b.required) - Number(a.required)).map(o => {
    const arg = o.argument;
    const placeholder = arg?.name === "VALUE" && arg.type === "choice" ? arg.choices?.join("|") : arg?.name === "VALUE" && arg.type === "digest" ? "ALGORITHM:HEX" : arg?.name;
    const name = o.long + (arg ? " " + placeholder : "") + (o.repeatable ? "..." : "");
    return o.required ? name : "[" + name + "]";
  }), ...(external ? ["[ARGUMENTS...]"] : [])].join(" ");
  if (!usage.startsWith(pattern)) throw new TypeError("Synopsis must start with the pattern.");
  const expected = options.find(o => o.long === "--expect");
  if (typeof effect === "object" && expected && (expected.argument?.type !== "digest" || expected.argument.algorithms?.join(",") !== "sha256" || expected.required || expected.repeatable || expected.hidden || !expected.requires.includes("--apply") || !Array.isArray(opts.schema?.required) || !opts.schema.required.includes("fingerprint"))) throw new TypeError("Transaction --expect requires the reserved declaration and required fingerprint schema.");
  return { id, pattern: pattern.split(" "), usage, purpose, effect, outputMode: mode,
    input: { synopsis: usage, operands, options, constraints, passthrough: external },
    outputSchema: opts.schema ?? { type: "object" }, exitCodes: { ...EXIT_CODES }, external,
    hidden: opts.hidden ?? false, available: opts.unavailable === undefined,
    ...(opts.unavailable !== undefined ? { unavailableReason: opts.unavailable, unavailableCode: opts.unavailableCode } : {}),
    ...(opts.protocol ? { protocol: opts.protocol } : {}), ...(opts.examples?.length ? { examples: opts.examples } : {}), handler };
}
export const read = (id: string, pattern: string, purpose: string, handler: Handler, opts: CommandOptions = {}) => make(id, pattern, purpose, handler, "read", "json-envelope", opts);
export const records = (id: string, pattern: string, purpose: string, handler: Handler, opts: CommandOptions = {}) => make(id, pattern, purpose, handler, "read", "json-records", opts);
export const execute = (id: string, pattern: string, purpose: string, handler: Handler, opts: CommandOptions = {}) => make(id, pattern, purpose, handler, "execute", "json-envelope", opts);
export const stream = (id: string, pattern: string, purpose: string, handler: Handler, opts: CommandOptions = {}) => make(id, pattern, purpose, handler, "stream", "protocol-stream", opts);
export const external = (id: string, pattern: string, purpose: string, handler: Handler, opts: CommandOptions = {}) => make(id, pattern, purpose, handler, "execute", "protocol-stream", opts, true);
export function transaction(id: string, pattern: string, purpose: string, handler: Handler, opts: CommandOptions & { commit?: boolean; expect?: boolean } = {}): Command {
  const options = [...(opts.options ?? []), flag("--apply", "Apply the reviewed plan; without it the command only plans.")];
  if (opts.expect) options.push(option("--expect", "Apply only the plan this fingerprint names.", argument("FINGERPRINT", "digest", { algorithms: ["sha256"] }), { requires: ["--apply"] }));
  return make(id, pattern, purpose, handler, { plan: "preview", apply: opts.commit ? "commit" : "apply" }, "json-envelope", { ...opts, options });
}
export function descriptor(command: Command, summary = false): Schema {
  const { handler: _handler, unavailableCode: _code, ...data } = command;
  if (!summary) return data;
  const { outputSchema: _schema, exitCodes: _exit, examples: _examples, ...small } = data;
  return small;
}
