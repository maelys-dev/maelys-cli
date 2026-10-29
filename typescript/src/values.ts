// SPDX-License-Identifier: MPL-2.0
import { Regex } from "./pattern.js";

export type Json = null | boolean | number | bigint | string | Json[] | { [key: string]: Json };
export type Schema = { [key: string]: unknown };
export class Failure extends Error {
  template?: string;
  templateArguments?: (string | number | bigint)[];
  constructor(public readonly code: string, message: string, public readonly hint = "",
    public readonly issues?: { code: string; message: string; path?: string | (string | number)[] }[],
    public readonly extensions?: Record<string, Json>) { super(message); }
}
/** A refusal of the line (agent-cli/v2 2.23.0, section 8, "What a refusal names"): the one entry of
 * error.issues, its reserved code and the path at fault, with the canonical message. */
export function refusal(code: string, message: string, hint: string | undefined, issue: string, path: string): Failure {
  return new Failure(code, message, hint ?? "", [{ code: issue, message, path }]);
}
/** Format a canonical failure before translating its human rendering. */
export function failure(code: string, template: string, args: (string | number | bigint)[] = [],
  options: { hint?: string; issues?: Failure["issues"]; extensions?: Failure["extensions"] } = {}): Failure {
  const result = new Failure(code, formatTemplate(template, args), options.hint, options.issues, options.extensions);
  Object.assign(result, { template, templateArguments: args });
  return result;
}
export function formatTemplate(template: string, args: (string | number | bigint)[]): string {
  let index = 0, result = "";
  for (let at = 0; at < template.length;) {
    if (template[at] !== "%") { result += template[at++]; continue; }
    const token = /^%(?:%|(?:hh|h|ll|l|z|j|t)?[diuoxXs])/.exec(template.slice(at))?.[0];
    if (!token) throw new TypeError("Unsupported failure conversion; use scalar C conversions without width or precision.");
    at += token.length;
    if (token === "%%") { result += "%"; continue; }
    if (index >= args.length) throw new TypeError("A failure template lacks an argument.");
    const argument = args[index++], kind = token.at(-1);
    result += kind === "s" ? String(argument) : kind === "x" ? BigInt(argument).toString(16)
      : kind === "X" ? BigInt(argument).toString(16).toUpperCase() : kind === "o" ? BigInt(argument).toString(8) : BigInt(argument).toString(10);
  }
  if (index !== args.length) throw new TypeError("A failure template has too many arguments.");
  return result;
}
export function stringify(value: unknown, compact = true): string {
  return JSON.stringify(value, (_key, child) => {
    if (typeof child === "bigint") return (JSON as typeof JSON & { rawJSON: (s: string) => unknown }).rawJSON(String(child));
    if (typeof child === "number" && !Number.isFinite(child)) throw new Failure("UNEXPECTED", "A reply contains a non-finite number.");
    return child;
  }, compact ? undefined : 2);
}
export type ValueKind = "string" | "boolean" | "integer" | "unsigned" | "size" | "duration" | "path" | "absolute-path" | "choice" | "hex" | "sha256" | "digest";
export type Value = { name: string; type?: ValueKind; choices?: string[]; minimum?: number | bigint; maximum?: number | bigint; digits?: number | number[]; algorithms?: string[]; pattern?: string };
export function argument(name: string, type: ValueKind = "string", bounds: Omit<Value, "name" | "type"> = {}): Value {
  const result = { name, type, ...bounds };
  validateValue(result);
  if (["unsigned", "size", "duration"].includes(type) && bounds.minimum === 0) delete result.minimum;
  return result;
}
export function validateValue(value: Value): void {
  if (!value.name || !["string", "boolean", "integer", "unsigned", "size", "duration", "path", "absolute-path", "choice", "hex", "sha256", "digest"].includes(value.type ?? "string")) throw new TypeError("Invalid value declaration.");
  if (value.pattern !== undefined) {
    if (!["string", "path"].includes(value.type ?? "string")) throw new TypeError("Patterns require string or path values.");
    new Regex(value.pattern);
  }
  if (value.type === "hex") {
    const widths = Array.isArray(value.digits) ? value.digits : [value.digits];
    if (value.minimum !== undefined || value.maximum !== undefined || !widths.length || widths.length > 2 ||
      widths.some(n => !Number.isInteger(n) || n! < 1) || new Set(widths).size !== widths.length) throw new TypeError("Hex values declare one or two distinct positive digits widths.");
  } else if (value.digits !== undefined) throw new TypeError("digits requires a hex value.");
  if (value.type === "choice" && (!value.choices?.length || value.choices.some(c => typeof c !== "string" || !c) || new Set(value.choices).size !== value.choices.length)) throw new TypeError("Choices must be nonempty and unique.");
  if (value.minimum !== undefined && value.maximum !== undefined && value.minimum > value.maximum) throw new TypeError("Minimum exceeds maximum.");
  for (const bound of [value.minimum, value.maximum]) if (bound !== undefined && typeof bound !== "bigint" && !Number.isSafeInteger(bound)) throw new TypeError("Numeric bounds outside JavaScript's exact range must use bigint.");
  if (value.algorithms && (value.type !== "digest" || !value.algorithms.length || value.algorithms.some(a => !["sha256", "sha384", "sha512", "sha1"].includes(a)))) throw new TypeError("Invalid digest algorithms.");
}
export function parseValue(spec: Value, text: string, where: string, usage: string): Json {
  // The path is the option's long (`Option --x`) or the operand's name.
  const path = where.startsWith("Option ") ? where.slice("Option ".length) : where;
  const refuse = (expected: string, cause = "VALUE_KIND"): never => { throw refusal("VALIDATION_FAILED", where + " takes " + expected + ", not '" + text + "'.", "Use '" + usage + "'.", cause, path); };
  if (typeof text !== "string") return refuse("a value");
  const kind = spec.type ?? "string";
  if (kind === "boolean") { if (!["true", "false"].includes(text)) return refuse("true or false"); return text === "true"; }
  if (kind === "string" || kind === "path") {
    if (kind === "path" && !text) return refuse("a non-empty path");
    if (spec.pattern && !new Regex(spec.pattern).search(text)) return refuse("a value matching " + spec.pattern, "VALUE_PATTERN");
    return text;
  }
  if (kind === "absolute-path") { if (!text.startsWith("/")) return refuse("an absolute path"); return text; }
  if (kind === "choice") { if (!spec.choices?.includes(text)) return refuse(spec.choices?.join(", ") ?? "a declared choice", "VALUE_CHOICE"); return text; }
  if (kind === "hex" || kind === "sha256") {
    const widths = kind === "sha256" ? [64] : Array.isArray(spec.digits) ? spec.digits : [spec.digits];
    if (!/^[0-9a-f]+$(?![\s\S])/.test(text) || !widths.includes(text.length)) return refuse(widths.join(" or ") + " lowercase hexadecimal digits");
    return text;
  }
  if (kind === "digest") {
    const match = /^([a-z0-9-]+):([0-9a-f]+)$(?![\s\S])/.exec(text);
    const digits: Record<string, number> = { sha256: 64, sha384: 96, sha512: 128, sha1: 40 };
    if (!match || !Object.hasOwn(digits, match[1]) || spec.algorithms && !spec.algorithms.includes(match[1]) || match[2].length !== digits[match[1]]) return refuse("ALGORITHM:HEX with " + (spec.algorithms?.join(", ") ?? "a declared algorithm"));
    return text;
  }
  let parsed: bigint;
  if (kind === "integer" || kind === "unsigned") {
    if (!(kind === "integer" ? /^-?[0-9]+$(?![\s\S])/ : /^[0-9]+$(?![\s\S])/).test(text)) return refuse(kind === "integer" ? "an integer" : "an unsigned integer");
    parsed = BigInt(text);
  } else {
    const match = (kind === "size" ? /^([0-9]+)([KMGT]?)$(?![\s\S])/ : /^([0-9]+)(ms|s|m|h|d)$(?![\s\S])/).exec(text);
    if (!match) return refuse(kind === "size" ? "a size such as 512, 4K, 16M, 2G or 1T" : "a duration with its unit: ms, s, m, h or d");
    const units: Record<string, bigint> = kind === "size" ? { "": 1n, K: 1024n, M: 1048576n, G: 1073741824n, T: 1099511627776n } : { ms: 1n, s: 1000n, m: 60000n, h: 3600000n, d: 86400000n };
    parsed = BigInt(match[1]) * units[match[2]];
  }
  if (parsed < (kind === "integer" ? -(1n << 63n) : 0n) || parsed > (kind === "integer" ? (1n << 63n) - 1n : (1n << 64n) - 1n)) return refuse(kind === "integer" ? "an integer" : "an unsigned integer");
  if (spec.minimum !== undefined && parsed < BigInt(spec.minimum) || spec.maximum !== undefined && parsed > BigInt(spec.maximum)) throw refusal("VALIDATION_FAILED", where + " must be between " + (spec.minimum ?? "-inf") + " and " + (spec.maximum ?? "+inf") + ", not " + text + ".", "Use '" + usage + "'.", "VALUE_RANGE", path);
  return parsed >= BigInt(Number.MIN_SAFE_INTEGER) && parsed <= BigInt(Number.MAX_SAFE_INTEGER) ? Number(parsed) : parsed;
}
export function parseFlag(value: string | null, name: string): boolean {
  if (value === null || ["true", "yes", "on", "1"].includes(value)) return true;
  if (["false", "no", "off", "0"].includes(value)) return false;
  throw refusal("VALIDATION_FAILED", "Option " + name + " takes true, false, yes, no, on, off, 1 or 0, not '" + value + "'.", undefined, "VALUE_KIND", name);
}
