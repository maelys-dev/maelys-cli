// SPDX-License-Identifier: MPL-2.0
// The same grammar and Thompson machine as c/src/pattern.c and Python.
type Node = { kind: string; value?: number | bigint; negated?: boolean; children?: Node[]; low?: number; high?: number | null };
type Instruction = { op: string; x?: number | bigint; y?: number };
export class RegexError extends Error {
  constructor(public readonly code: string, message: string, public readonly offset: number) { super(message); }
}

function tree(pattern: string): Node {
  let at = 0;
  const fail = (code: string, message: string): never => { throw new RegexError(code, message, Buffer.byteLength(pattern.slice(0, at))); };
  const peek = () => pattern[at] ?? "";
  if (!pattern) fail("empty", "an empty pattern");
  const bound = (): [number, number | null] => {
    const parts = [""];
    while (true) {
      if (at >= pattern.length) fail("bound", "an unclosed bound");
      const c = pattern[at++];
      if (/[0-9]/.test(c)) {
        if (parts.at(-1) === "0") fail("bound", "a bound written with a leading zero");
        parts[parts.length - 1] += c;
      } else if (c === "," && parts.length === 1) parts.push("");
      else if (c === "}") break;
      else fail("bound", "invalid bound");
    }
    if (!parts[0]) fail("bound", "a bound without its minimum");
    const low = Number(parts[0]), high = parts.length === 1 ? low : parts[1] ? Number(parts[1]) : null;
    if (low > 255 || high !== null && high > 255) fail("limit", "a bound above 255");
    if (high !== null && low > high) fail("bound", "a bound with minimum above maximum");
    return [low, high];
  };
  const characterClass = (): Node => {
    const negated = peek() === "^";
    if (negated) at++;
    let mask = 0n, items = 0, afterRange = false;
    while (true) {
      if (at >= pattern.length) fail("bracket", "an unclosed bracket expression");
      const c = peek();
      if (c === "-" && afterRange && at + 1 < pattern.length && pattern[at + 1] !== "]") fail("bracket", "ambiguous dash after a range");
      if (c === "]") {
        if (!items) fail("bracket", "an empty bracket expression");
        at++; return { kind: "class", value: mask, negated };
      }
      if (c === "[" || c === "\\") fail("bracket", "nonportable bracket expression");
      if (c < " " || c > "~") fail("encoding", "a character outside printable ASCII");
      at++;
      let low = c.charCodeAt(0), high = low;
      afterRange = false;
      if (peek() === "-" && at + 1 < pattern.length && pattern[at + 1] !== "]") {
        const end = pattern[at + 1];
        if (end === "[" || end === "\\") fail("bracket", "invalid range end");
        if (end < " " || end > "~") fail("encoding", "a character outside printable ASCII");
        if (end < c) fail("bracket", "a range whose ends are reversed");
        high = end.charCodeAt(0); at += 2; afterRange = true;
      }
      mask |= ((1n << BigInt(high - low + 1)) - 1n) << BigInt(low);
      items++;
    }
  };
  const atom = (depth: number): Node => {
    const c = peek();
    if (c === "(") {
      at++;
      if (peek() === "?") fail("group", "nonportable group");
      if (depth + 1 > 32) fail("limit", "more than 32 nested groups");
      const child = alternation(depth + 1);
      if (peek() !== ")") fail("group", "an unclosed group");
      at++; return child;
    }
    if (c === "[") { at++; return characterClass(); }
    if ([".", "^", "$"].includes(c)) { at++; return { kind: ({ ".": "any", "^": "bol", "$": "eol" } as Record<string, string>)[c] }; }
    if ([")", "]", "}"].includes(c)) fail("group", "an unmatched or unescaped delimiter");
    if (["*", "+", "?", "{"].includes(c)) fail("quantifier", "a quantifier that follows nothing");
    if (c === "\\") {
      at++;
      if (at >= pattern.length) fail("escape", "an escape that ends the pattern");
      const escaped = peek();
      if (!".*+?()[]{}|^$\\".includes(escaped)) fail("escape", "an escape outside the common subset");
      at++; return { kind: "char", value: escaped.charCodeAt(0) };
    }
    if (c < " " || c > "~") fail("encoding", "a character outside printable ASCII");
    at++; return { kind: "char", value: c.charCodeAt(0) };
  };
  const piece = (depth: number): Node => {
    const child = atom(depth), c = peek();
    if (!["*", "+", "?", "{"].includes(c)) return child;
    if (["bol", "eol"].includes(child.kind)) fail("quantifier", "a quantified anchor");
    at++;
    const [low, high] = c === "{" ? bound() : c === "*" ? [0, null] : c === "+" ? [1, null] : [0, 1];
    if (["*", "+", "?", "{"].includes(peek())) fail("quantifier", "two quantifiers in a row");
    return { kind: "repeat", children: [child], low, high };
  };
  const sequence = (depth: number): Node => {
    const children: Node[] = [];
    while (at < pattern.length && !["|", ")"].includes(peek())) children.push(piece(depth));
    if (!children.length) fail("empty", "an empty alternative or group");
    return children.length === 1 ? children[0] : { kind: "cat", children };
  };
  const alternation = (depth: number): Node => {
    const children = [sequence(depth)];
    while (peek() === "|") { at++; children.push(sequence(depth)); }
    return children.length === 1 ? children[0] : { kind: "alt", children };
  };
  const root = alternation(0);
  if (at < pattern.length) fail("group", "a group that closes nothing");
  const size = (node: Node): number => {
    if (node.kind === "cat") return node.children!.reduce((n, child) => n + size(child), 0);
    if (node.kind === "alt") return node.children!.reduce((n, child) => n + size(child), 0) + 2 * (node.children!.length - 1);
    if (node.kind === "repeat") {
      const child = size(node.children![0]);
      return node.high === null ? child * (node.low! + 1) + 2 : child * node.high! + node.high! - node.low!;
    }
    return 1;
  };
  if (size(root) + 1 > 65536) fail("limit", "more than 65536 states");
  return root;
}

function compile(node: Node): Instruction[] {
  const code: Instruction[] = [];
  const emit = (op: string, x?: number | bigint, y?: number) => { code.push({ op, x, y }); return code.length - 1; };
  const walk = (n: Node): void => {
    if (["char", "any", "bol", "eol", "class"].includes(n.kind)) { emit(n.kind, n.value, n.negated ? 1 : 0); return; }
    if (n.kind === "cat") { n.children!.forEach(walk); return; }
    if (n.kind === "alt") {
      const jumps = [];
      for (const child of n.children!.slice(0, -1)) {
        const split = emit("split", code.length + 1);
        walk(child); jumps.push(emit("jump")); code[split].y = code.length;
      }
      walk(n.children!.at(-1)!);
      for (const jump of jumps) code[jump].x = code.length;
      return;
    }
    for (let i = 0; i < n.low!; i++) walk(n.children![0]);
    if (n.high === null) {
      const split = emit("split", code.length + 1);
      walk(n.children![0]); emit("jump", split); code[split].y = code.length;
    } else {
      const splits = [];
      for (let i = n.low!; i < n.high!; i++) { splits.push(emit("split", code.length + 1)); walk(n.children![0]); }
      for (const split of splits) code[split].y = code.length;
    }
  };
  walk(node); emit("match"); return code;
}

function* characters(value: string | Uint8Array): Generator<number> {
  if (typeof value === "string") { for (const c of value) yield c.codePointAt(0)!; return; }
  for (let i = 0; i < value.length;) {
    const first = value[i];
    let count = first < 128 ? 1 : first >= 194 && first <= 223 ? 2 : first >= 224 && first <= 239 ? 3 : first >= 240 && first <= 244 ? 4 : 0;
    if (i + count > value.length) count = 0;
    let code = first & (count === 2 ? 31 : count === 3 ? 15 : count === 4 ? 7 : 127);
    for (let j = 1; j < count; j++) {
      if ((value[i + j] & 192) !== 128) { count = 0; break; }
      code = code * 64 + (value[i + j] & 63);
    }
    if (count > 1 && (code < [0, 0, 128, 2048, 65536][count] || code > 0x10ffff || code >= 0xd800 && code <= 0xdfff)) count = 0;
    if (!count) { yield 0x110000 + first; i++; } else { yield code; i += count; }
  }
}

/** Linear-time matching over the pinned common regex subset; no V8 backtracking. */
export class Regex {
  private readonly code: Instruction[];
  constructor(public readonly pattern: string) { this.code = compile(tree(pattern)); }
  get states() { return this.code.length; }
  search(value: string | Uint8Array): boolean { return this.run(value, 0); }
  private run(value: string | Uint8Array, initialGeneration: number): boolean {
    const input = characters(value), marks = new Uint32Array(this.code.length);
    let pending = input.next();
    let generation = initialGeneration >>> 0;
    const advance = () => {
      generation = (generation + 1) >>> 0;
      if (!generation) { marks.fill(0); generation = 1; }
    };
    const add = (start: number, first: boolean, last: boolean, out: number[]): boolean => {
      const stack = [start];
      while (stack.length) {
        const pc = stack.pop()!;
        if (marks[pc] === generation) continue;
        marks[pc] = generation;
        const op = this.code[pc];
        if (op.op === "jump") stack.push(Number(op.x));
        else if (op.op === "split") stack.push(op.y!, Number(op.x));
        else if (op.op === "bol") { if (first) stack.push(pc + 1); }
        else if (op.op === "eol") { if (last) stack.push(pc + 1); }
        else if (op.op === "match") return true;
        else out.push(pc);
      }
      return false;
    };
    advance();
    let current: number[] = [];
    if (add(0, true, pending.done ?? false, current)) return true;
    while (!pending.done) {
      advance();
      const c = pending.value;
      pending = input.next();
      const following: number[] = [], last = pending.done ?? false;
      for (const pc of current) {
        const op = this.code[pc];
        const inside = op.op === "class" && c < 128 && (BigInt(op.x!) >> BigInt(c) & 1n) === 1n;
        const accepted = op.op === "char" ? op.x === c : op.op === "any" ? ![10, 13, 0x2028, 0x2029].includes(c) : op.y ? !inside : inside;
        if (accepted && add(pc + 1, false, last, following)) return true;
      }
      if (add(0, false, last, following)) return true;
      current = following;
    }
    return false;
  }
}
