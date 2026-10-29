// SPDX-License-Identifier: MPL-2.0
import { spawnSync } from "node:child_process";
import { builtins } from "./builtins.js";
import { completionTemplates } from "./completion-templates.js";
import { descriptor, type Command, type Option, type Result } from "./catalog.js";
import { Failure, formatTemplate, parseValue, stringify, validateValue, type Json, type Schema } from "./values.js";
import { parse, environmentFormat, leadingGlobals } from "./parser.js";
import { fileFailure } from "./files.js";
import { pagerArguments } from "./terminal.js";
import { frameworkTranslations } from "./translations.js";
const safe = (text: string) => text.replace(/[\x00-\x08\x0b-\x1f\x7f]/g, "?");
function checkedFailure(failure: Failure): Failure {
  try {
    if (typeof failure.code !== "string" || !/^[A-Z][A-Z0-9_]*$(?![\s\S])/.test(failure.code) || typeof failure.message !== "string" || typeof failure.hint !== "string") throw new Error();
    if (failure.issues !== undefined && (!Array.isArray(failure.issues) || failure.issues.length > 64 || failure.issues.some(i => !i || typeof i !== "object" || Array.isArray(i) || typeof i.code !== "string" || typeof i.message !== "string" || Object.keys(i).some(k => !["code", "message", "path"].includes(k)) || i.path !== undefined && typeof i.path !== "string" && (!Array.isArray(i.path) || i.path.some(p => typeof p !== "string" && (typeof p !== "number" || !Number.isSafeInteger(p))))))) throw new Error();
    if (failure.extensions !== undefined && (!failure.extensions || typeof failure.extensions !== "object" || Array.isArray(failure.extensions) || Object.keys(failure.extensions).length > 64 || Object.keys(failure.extensions).some(k => !/^x-[a-z0-9-]+$(?![\s\S])/.test(k)))) throw new Error();
    stringify({ issues: failure.issues, extensions: failure.extensions });
    if (failure.template !== undefined && (typeof failure.template !== "string" || !Array.isArray(failure.templateArguments) || formatTemplate(failure.template, failure.templateArguments) !== failure.message)) throw new Error();
    return failure;
  } catch { return new Failure("UNEXPECTED", "The handler returned an invalid failure."); }
}
export const terminalColor = (mode: string, tty = Boolean(process.stdout.isTTY)): boolean => {
  if (mode === "never") return false;
  if (mode === "always" || process.env.CLICOLOR_FORCE && process.env.CLICOLOR_FORCE !== "0") return true;
  return !process.env.NO_COLOR && Boolean(process.env.TERM) && process.env.TERM !== "dumb" && tty;
};
const cell = (value: Json): string => typeof value === "string" ? value.replace(/[\\\x00-\x1f\x7f]/g, c => ({ "\\": "\\\\", "\n": "\\n", "\r": "\\r", "\t": "\\t" })[c] ?? "\\u" + c.charCodeAt(0).toString(16).padStart(4, "0")) : stringify(value);
export function recordText(records: Json[]): string {
  const objects = records.filter(v => v && !Array.isArray(v) && typeof v === "object") as Record<string, Json>[];
  const keys = [...new Set(objects.flatMap(v => Object.keys(v)))].sort((a, b) => {
    const left = [...a].map(c => c.codePointAt(0)!), right = [...b].map(c => c.codePointAt(0)!);
    for (let i = 0; i < Math.min(left.length, right.length); i++) if (left[i] !== right[i]) return left[i] - right[i];
    return left.length - right.length;
  });
  return objects.map(row => keys.map(key => Object.hasOwn(row, key) ? cell(row[key]) : "").join("\t") + "\n").join("");
}
export function fieldText(value: Json): string {
  if (Array.isArray(value)) return value.length && value.every(v => v && typeof v === "object" && !Array.isArray(v)) ? recordText(value) : value.map(v => cell(v) + "\n").join("");
  if (value && typeof value === "object") return recordText([value]);
  return cell(value) + "\n";
}
export class Invocation {
  operands: Json[] = [];
  rawOperands: string[] = [];
  readonly options = new Map<string, Json>();
  format = "text"; compact = false; nonInteractive = false; verbose = false;
  color = "auto"; pager = "auto"; progress = "auto"; field?: string; lang?: string;
  expectChecked = false;
  private progressShown = false;
  constructor(public readonly program: Program, public command: Command) {}
  option(name: string, fallback: Json = null): Json {
    if (this.options.has(name)) return this.options.get(name)!;
    const definition = this.command.input.options.find(o => o.long === name);
    return definition?.argument && definition.default !== undefined ? parseValue(definition.argument, definition.default, "Option " + name, this.command.usage) : fallback;
  }
  flag(name: string): boolean { return Boolean(this.options.get(name)); }
  get apply(): boolean { return this.flag("--apply"); }
  get colorStdout(): boolean { return terminalColor(this.color); }
  get colorStderr(): boolean { return terminalColor(this.color, Boolean(process.stderr.isTTY)); }
  get progressWanted(): boolean { return this.format === "text" && this.progress !== "never" && (this.progress === "always" || Boolean(process.stderr.isTTY)); }
  detail(text: string): void { if (this.verbose && this.format === "text") { this.progressDone(); process.stderr.write(this.program.program + ": " + safe(text) + "\n"); } }
  showProgress(text: string): void {
    if (!this.progressWanted) return;
    if (process.stderr.isTTY) { process.stderr.write("\r" + safe(text) + "\x1b[K"); this.progressShown = true; }
    else process.stderr.write(safe(text) + "\n");
  }
  progressDone(): void { if (this.progressShown) { process.stderr.write("\r\x1b[K"); this.progressShown = false; } }
  expect(fingerprint: string): void {
    this.expectChecked = true;
    const expected = this.options.get("--expect");
    if (expected !== undefined && expected !== fingerprint) throw new Failure("PRECONDITION_FAILED", "The plan --expect names is no longer the one '" + this.command.id + "' would apply: the action or the state it touches has changed. Nothing was written.", "Plan again without --apply, review the new plan, then apply it with its fingerprint.");
  }
}
export type ProgramOptions = {
  guide?: string; defaultCommand?: string; canonicalLanguage?: string;
  translations?: Record<string, Record<string, string>>;
  text?: Record<string, (data: Json) => string>;
};
export class Program {
  readonly catalog: Command[];
  readonly globalOptions: Option[] = structuredClone(builtins.globalOptions) as unknown as Option[];
  readonly defaultCommand?: string;
  readonly canonicalLanguage: string;
  resolvedCommand = "unknown";
  pendingFamily?: string[];
  private humanLanguage: string;
  constructor(public readonly program: string, public readonly product: string, public readonly version: string,
    commands: Command[], public readonly options: ProgramOptions = {}) {
    if (!/^[A-Za-z0-9][A-Za-z0-9._-]*$(?![\s\S])/.test(program)) throw new TypeError("Program must be a safe executable name.");
    this.canonicalLanguage = options.canonicalLanguage ?? "en";
    const lang = this.globalOptions.find(o => o.long === "--lang")!.argument!;
    parseValue(lang, this.canonicalLanguage, "Canonical language", "");
    for (const [tag, table] of Object.entries(options.translations ?? {})) {
      parseValue(lang, tag, "Translation language", "");
      if (!table || typeof table !== "object" || Object.values(table).some(v => typeof v !== "string" || /[\x00-\x08\x0b-\x1f\x7f]/.test(v))) throw new TypeError("Invalid translation table.");
    }
    this.humanLanguage = this.canonicalLanguage;
    this.catalog = (structuredClone(builtins.commands) as unknown as Command[]).map(command => ({ ...command, handler: i => this.builtin(command.id, i) }));
    for (const command of commands) {
      if (!/^[a-z][a-z0-9-]*(\.[a-z0-9][a-z0-9-]*)*$(?![\s\S])/.test(command.id) || command.id === "unknown" || typeof command.handler !== "function") throw new TypeError("Invalid command declaration.");
      for (const value of [...command.input.operands, ...command.input.options.flatMap(o => o.argument ? [o.argument] : [])]) validateValue(value);
      for (const opt of command.input.options) {
        if (opt.hidden && opt.required) throw new TypeError("A hidden option cannot be required.");
        if (opt.default !== undefined && opt.argument) parseValue(opt.argument, opt.default, opt.long, command.usage);
        for (const other of [...opt.requires, ...opt.conflictsWith]) if (other === opt.long || !command.input.options.some(o => o.long === other) && !command.input.operands.some(o => o.name === other)) throw new TypeError("Invalid option reference.");
      }
      if (["preview", "apply", "commit"].includes(String(command.effect))) throw new TypeError("Write phases require a transaction.");
      if (command.input.passthrough && !command.external || command.external && (command.effect !== "execute" || command.protocol)) throw new TypeError("Invalid delegate declaration.");
      if (command.input.options.some(o => o.long === "--apply") && typeof command.effect !== "object") throw new TypeError("--apply is reserved for transactions.");
      if (this.catalog.some(c => c.id === command.id || c.pattern.join(" ") === command.pattern.join(" "))) throw new TypeError("Duplicate command identifier or pattern.");
      if (command.input.options.some(o => this.globalOptions.some(g => g.long === o.long))) throw new TypeError("A command cannot shadow a global option.");
      const freeze = <T>(value: T): T => {
        if (value && typeof value === "object") { for (const child of Object.values(value)) freeze(child); Object.freeze(value); }
        return value;
      };
      const snapshot = structuredClone(descriptor(command)) as unknown as Command;
      this.catalog.push(freeze({ ...snapshot, handler: command.handler, ...(command.unavailableCode ? { unavailableCode: command.unavailableCode } : {}) }));
    }
    this.defaultCommand = options.defaultCommand;
    if (this.defaultCommand !== undefined) {
      const target = this.catalog.find(c => c.id === this.defaultCommand);
      if (!target || this.catalog.indexOf(target) < 5 || !target.available || target.hidden || target.external || target.effect === "execute") throw new TypeError("Invalid default command.");
    }
    for (const command of commands) for (const example of command.examples ?? []) {
      const invocation = this.parse(example.words);
      if (invocation.command.id !== command.id) throw new TypeError("Example does not invoke its declared command.");
    }
  }
  parse(argv: string[]): Invocation { this.pendingFamily = undefined; return parse(this, argv); }
  commandById(id: string): Command {
    const found = this.catalog.find(c => c.id === id);
    if (!found) throw new Failure("INVALID_COMMAND", "Unknown command identifier '" + id + "'.", "Run describe --summary to list the command identifiers.");
    return found;
  }
  resolve(words: string[]): [Command | undefined, number] {
    let found: Command | undefined;
    for (const command of this.catalog) if (command.pattern.every((word, i) => words[i] === word) && (!found || command.pattern.length > found.pattern.length)) found = command;
    return [found, found?.pattern.length ?? 0];
  }
  family(prefix?: string, words?: string[]): Command[] {
    return this.catalog.filter(c => !c.hidden && (prefix ? c.id.startsWith(prefix + ".") : words ? words.length < c.pattern.length && words.every((w, i) => c.pattern[i] === w) : true));
  }
  private language(option?: string): void {
    let requested = option || process.env.MAELYS_CLI_LANG;
    if (!requested) {
      const locale = process.env.LC_ALL || process.env.LC_MESSAGES || process.env.LANG || "";
      requested = /^(C|POSIX)(\.|$)/i.test(locale) ? this.canonicalLanguage : locale.split(/[.@]/)[0].replace(/_/g, "-");
    }
    const available = [this.canonicalLanguage, ...Object.keys(this.options.translations ?? {})];
    let tag = requested?.toLowerCase() ?? "";
    while (tag) {
      const match = available.find(l => l.toLowerCase() === tag);
      if (match) { this.humanLanguage = match; return; }
      tag = tag.includes("-") ? tag.slice(0, tag.lastIndexOf("-")) : "";
    }
    this.humanLanguage = this.canonicalLanguage;
  }
  private tr(text: string): string { return this.options.translations?.[this.humanLanguage]?.[text] ?? frameworkTranslations[this.humanLanguage.toLowerCase()]?.[text] ?? text; }
  private help(commands: Command[], target?: Command, family?: string): string {
    const out = [this.product + " " + this.version, "", this.tr(target?.purpose ?? this.options.guide ?? this.product), "", this.tr("USAGE")];
    if (target) {
      out.push("  " + this.program + " " + target.usage.replace(/ \[--[^\]]+\]/g, "") + (target.input.options.some(o => !o.required && !o.hidden) ? " [options]" : ""));
      if (!target.available) out.push("  " + this.tr("Unavailable") + ": " + this.tr(target.unavailableReason ?? ""));
    } else if (this.defaultCommand && !family) out.push("  " + this.program + " [options] [-- OPERANDS]", "  " + this.program + " " + this.commandById(this.defaultCommand).pattern.join(" ") + " [options] [-- OPERANDS]");
    else out.push("  " + this.program + " " + (family ?? "COMMAND") + " [options]");
    if (!target) out.push("", this.tr("COMMANDS"), ...commands.filter(c => !c.hidden).map(c => "  " + c.pattern.join(" ").padEnd(18) + this.tr(c.purpose) + (!c.available ? " (unavailable)" : "")));
    if (target?.input.operands.length) out.push("", this.tr("OPERANDS"), ...target.input.operands.map(o => "  " + o.name.padEnd(18) + this.tr(o.summary)));
    const opts = [...(target?.input.options ?? []), ...this.globalOptions].filter(o => !o.hidden);
    if (opts.length) out.push("", this.tr("OPTIONS"), ...opts.map(o => "  " + (o.long + (o.argument ? " " + o.argument.name : "")).padEnd(24) + this.tr(o.summary)));
    if (target?.examples?.length) out.push("", this.tr("EXAMPLES"), ...target.examples.flatMap(e => ["  " + this.program + " " + e.words.join(" "), "    " + this.tr(e.summary)]));
    return out.join("\n") + "\n";
  }
  private builtin(id: string, invocation: Invocation): Result {
    if (id === "version") return { data: { product: this.product, program: this.program, version: this.version, contract: "agent-cli/v2", cliApi: 1, framework: "maelys_cli typescript", lang: this.canonicalLanguage } };
    if (id === "describe") {
      const summary = invocation.flag("--summary"), prefix = invocation.option("--prefix"), operand = invocation.operands[0];
      let commands = this.catalog;
      const data: Schema = { schemaVersion: 1, program: this.program, product: this.product, version: this.version, contract: "agent-cli/v2", cliApi: 1, framework: "maelys_cli typescript", ...(this.defaultCommand !== undefined ? { defaultCommand: this.defaultCommand } : {}) };
      if (typeof prefix === "string") {
        commands = this.catalog.filter(c => c.id === prefix || c.id.startsWith(prefix + "."));
        if (!commands.length) throw new Failure("INVALID_COMMAND", "No command in namespace: " + prefix + ".", "Run describe --summary --format json and select a returned command identifier.");
        data.filter = { kind: "command-prefix", value: prefix };
      } else if (operand !== undefined) commands = [this.commandById(String(operand))];
      data.kind = operand !== undefined ? "command" : summary ? "summary" : "catalog";
      data.commands = commands.map(c => descriptor(c, summary && operand === undefined));
      if (data.kind === "catalog") { data.globalOptions = this.globalOptions; data.invariants = builtins.invariants; data.output = builtins.output; data["x-maelys-summary"] = this.options.guide ?? this.product; }
      return { data: data as Json };
    }
    if (id === "help") {
      let target: Command | undefined, commands = this.catalog.filter(c => !c.hidden), name: string | undefined;
      if (this.pendingFamily) { commands = this.family(undefined, this.pendingFamily); name = this.pendingFamily.join(" "); }
      else if (invocation.operands.length) {
        const query = String(invocation.operands[0]);
        target = this.catalog.find(c => c.id === query); commands = target ? [target] : this.family(query);
        if (!commands.length) throw new Failure("INVALID_COMMAND", "Unknown command identifier or family: " + query + ".", "Run 'help' without operands to list the commands and their families.");
        if (!target) name = query;
      }
      return { data: { text: this.help(commands, target, name), commands: commands.map(c => c.id) } };
    }
    if (id === "completion") {
      const shell = String(invocation.operands[0]) as keyof typeof completionTemplates;
      const script = completionTemplates[shell].replaceAll("__FUNCTION_PROGRAM__", this.program.replace(/[^A-Za-z0-9]/g, "_")).replaceAll("__PROGRAM__", this.program);
      return { data: { shell, script } };
    }
    const words = this.complete(invocation.rawOperands);
    return { data: { count: words.length, records: words.map(word => ({ word })) } };
  }
  complete(words: string[]): string[] {
    const current = words.at(-1) ?? "", previous = words.slice(0, -1);
    const shown = this.catalog.filter(c => !c.hidden && c.available);
    let [command, consumed] = this.resolve(previous);
    if (command && (command.hidden || !command.available)) command = undefined;
    if (!command && this.defaultCommand && previous.length && previous[0].startsWith("-") && !["--help", "--version"].includes(previous[0])) { command = this.commandById(this.defaultCommand); consumed = 0; }
    let candidates: string[] = [];
    const valueCandidates = (value: Option["argument"]) => value?.type === "choice" ? value.choices ?? [] : value?.type === "digest" ? (value.algorithms ?? []).map(a => a + ":") : [];
    if (!command) {
      candidates = shown.filter(c => c.pattern.length > previous.length && previous.every((word, i) => c.pattern[i] === word)).map(c => c.pattern[previous.length]);
      if (this.defaultCommand && !previous.length && current.startsWith("-")) candidates.push(...[...this.commandById(this.defaultCommand).input.options, ...this.globalOptions].filter(o => !o.hidden).map(o => o.long));
    } else if (!command.external) {
      const after = previous.slice(consumed), options = [...command.input.options, ...this.globalOptions];
      const expecting = after.at(-1)?.startsWith("--") && !after.at(-1)?.includes("=") ? options.find(o => o.long === after.at(-1) && o.argument) : undefined;
      if (expecting) candidates = valueCandidates(expecting.argument);
      else if (current.startsWith("--") && current.includes("=")) {
        const name = current.split("=")[0], option = command.input.options.find(o => o.long === name && !o.hidden && o.argument?.type === "choice");
        candidates = valueCandidates(option?.argument).map(c => name + "=" + c);
      } else if (current.startsWith("--")) {
        const given = new Set(after.filter(w => w.startsWith("--")).map(w => w.split("=")[0]));
        candidates = [...command.input.options, ...(command.outputMode === "protocol-stream" ? [] : this.globalOptions)].filter(o => !o.hidden && (o.repeatable || !given.has(o.long))).map(o => o.long);
      } else if (["help", "describe"].includes(command.id) && !after.length) candidates = shown.map(c => c.id);
      else if (command.input.operands.length) {
        let position = 0;
        for (let i = 0; i < after.length; i++) {
          if (after[i] === "--") { position += after.length - i - 1; break; }
          if (after[i].startsWith("--")) { if (options.find(o => o.long === after[i] && o.argument)) i++; } else position++;
        }
        const operands = command.input.operands, spec = position < operands.length ? operands[position] : operands.at(-1)?.variadic ? operands.at(-1) : undefined;
        candidates = valueCandidates(spec);
      }
    }
    // A callable parent does not hide longer patterns in the same family.
    // Keep operand choices first and deduplicate them with the child words.
    if (command && !command.external) candidates = [...candidates, ...shown.filter(c =>
      c.pattern.length > previous.length && previous.every((word, i) => c.pattern[i] === word))
      .map(c => c.pattern[previous.length])];
    // At the root, before any word but global options, none included, the line is still the program's: a word
    // that starts with a dash may be --version, as the line would read it (agent-cli/v2 2.22.0, section 6).
    const leading = current.startsWith("-") && leadingGlobals(this, previous) === previous.length ? ["--version"] : [];
    return [...new Set([...leading, ...candidates])].filter(c => c.startsWith(current));
  }
  private envelope(command: string, ok: boolean, exitCode: number, value: Json, compact: boolean): string { return stringify({ schemaVersion: 2, contract: "agent-cli/v2", command, ok, exitCode, [ok ? "data" : "error"]: value }, compact) + "\n"; }
  private page(text: string, invocation: Invocation): void {
    if (invocation.format === "text" && process.stdout.isTTY && invocation.pager !== "never" && !invocation.nonInteractive) {
      const args = pagerArguments(process.env.PAGER);
      if (args) {
        // The sole user-selected pager exception, as in the Python runtime.
        // No shell is started and quoted words are not expanded.
        const reply = spawnSync(args[0], args.slice(1), { input: text, stdio: ["pipe", "inherit", "inherit"],
          env: process.env.PAGER === undefined ? { ...process.env, LESS: process.env.LESS ?? "FRX" } : process.env });
        if (!reply.error) return;
      }
    }
    process.stdout.write(text);
  }
  async main(argv = process.argv.slice(2)): Promise<number> {
    this.resolvedCommand = "unknown";
    let invocation: Invocation | undefined, format = environmentFormat(), compact = argv.includes("--compact") || argv.includes("--pretty=false");
    for (let i = 0; i < argv.length; i++) if (["--json", "--format=json", "--format=jsonl"].includes(argv[i]) || argv[i] === "--format" && ["json", "jsonl"].includes(argv[i + 1])) format = "json";
    this.language();
    try {
      invocation = this.parse(argv); format = invocation.format; compact = invocation.compact; this.language(invocation.lang);
      const command = invocation.command;
      if (invocation.field !== undefined) {
        if (format === "json") throw new Failure("VALIDATION_FAILED", "--field conflicts with --format json: a filtered envelope would not validate against the command's outputSchema.", "Use --format text or --format jsonl with --field.");
        const required = command.outputSchema.required;
        if (command.effect !== "read" && (!Array.isArray(required) || !required.includes(invocation.field))) throw new Failure("VALIDATION_FAILED", "Option --field names a member the command's output schema does not require.", "Use a member the command's output schema requires; describe lists them.");
      }
      const reply = await command.handler(invocation);
      invocation.progressDone();
      if (command.outputMode === "protocol-stream") {
        const code = typeof reply === "number" ? reply : reply.exitCode ?? 0;
        if (!Number.isInteger(code) || code < 0 || code > 255) throw new Failure("UNEXPECTED", "A stream handler returned an invalid exit code.");
        return code;
      }
      if (typeof reply === "number") throw new Failure("UNEXPECTED", "A non-stream handler returned no data.");
      const { data, exitCode = 0 } = reply;
      if (![0, 2].includes(exitCode)) throw new Failure("UNEXPECTED", "A success result has an invalid exit code.");
      if (typeof command.effect === "object" && invocation.options.has("--expect") && !invocation.expectChecked) throw new Failure("UNEXPECTED", "Command '" + command.id + "' answered without checking --expect.", "Report this defect to the command implementation.");
      if (invocation.field !== undefined) {
        if (!data || typeof data !== "object" || Array.isArray(data) || !Object.hasOwn(data, invocation.field)) throw new Failure("VALIDATION_FAILED", "Option --field names '" + invocation.field + "', which '" + command.id + "' does not have.", "Use a top-level member of the command's data.");
        const value = data[invocation.field];
        if (format === "jsonl") process.stdout.write((Array.isArray(value) ? value : [value]).map(v => stringify(v) + "\n").join("")); else this.page(fieldText(value), invocation);
      } else if (format === "json") process.stdout.write(this.envelope(command.id, true, exitCode, data, compact));
      else if (format === "jsonl") {
        const records = (data as Record<string, Json>).records;
        if (!Array.isArray(records)) throw new Failure("UNEXPECTED", "A records reply must contain records.");
        process.stdout.write(records.map(v => stringify(v) + "\n").join(""));
      } else {
        let text: string; const renderer = this.options.text?.[command.id];
        if (renderer) text = renderer(data);
        else if (command.id === "help") text = String((data as Record<string, Json>).text);
        else if (command.id === "completion") text = String((data as Record<string, Json>).script);
        else if (command.id === "version") text = this.program + " " + this.version + "\n";
        else if (command.outputMode === "json-records") text = recordText((data as Record<string, Json>).records as Json[]);
        else text = stringify(data, false) + "\n";
        if (command.id === "help" && invocation.colorStdout) text = "\x1b[36m" + text + "\x1b[0m";
        this.page(text, invocation);
      }
      return exitCode;
    } catch (error) {
      invocation?.progressDone();
      this.language(invocation?.lang);
      let failure = error instanceof Failure ? error : error instanceof Error && "code" in error && typeof error.code === "string" ? fileFailure(error as NodeJS.ErrnoException) : new Failure("UNEXPECTED", error instanceof Error ? error.message : String(error));
      failure = checkedFailure(failure);
      const payload: Record<string, Json> = { code: failure.code, message: failure.message, ...(failure.hint ? { hint: failure.hint } : {}), ...(failure.issues?.length ? { issues: failure.issues as Json[] } : {}), ...failure.extensions };
      if (format === "text") {
        const color = invocation ? invocation.colorStderr : terminalColor(argv.some((word, index) => word === "--color=never" || word === "--color" && argv[index + 1] === "never") ? "never" : "auto", Boolean(process.stderr.isTTY));
        let message = this.tr(failure.message);
        if (failure.template) {
          try { message = formatTemplate(this.tr(failure.template), failure.templateArguments!); }
          catch { message = failure.message; } // A broken human translation cannot break failure reporting.
        }
        process.stderr.write((color ? "\x1b[31m" : "") + this.program + ": [" + failure.code + "] " + safe(message) + (color ? "\x1b[0m" : "") + "\n");
        if (failure.hint) process.stderr.write(this.tr("Hint:") + " " + safe(this.tr(failure.hint)) + "\n");
      } else process.stderr.write(this.envelope(invocation?.command.id ?? this.resolvedCommand, false, 1, payload, compact));
      return 1;
    }
  }
}
