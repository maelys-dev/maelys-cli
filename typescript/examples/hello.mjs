#!/usr/bin/env node
// SPDX-License-Identifier: MPL-2.0
import { readFileSync, existsSync } from "node:fs";
import * as cli from "../dist/index.js";
const schema = name => JSON.parse(readFileSync(new URL("../../c/examples/hello/schemas/" + name + ".json", import.meta.url), "utf8"));
export const PROGRAM = new cli.Program("maelys-hello-js", "Maelys Hello (JavaScript)", "0.1.0", [
  cli.read("greet", "greet", "Greet someone.", i => {
    const name = String(i.operands[0]), times = Number(i.option("--times"));
    let greeting = "Hello, " + name + "!";
    if (i.flag("--shout")) greeting = greeting.toUpperCase();
    i.detail("greeting " + name + " " + times + " time(s)");
    i.showProgress("greeting " + name); i.progressDone();
    return { data: { greeting, times, lines: Array(times).fill(greeting) } };
  }, {
    operands: [cli.operand("NAME", "Person or system to greet.")],
    options: [cli.flag("--shout", "Render the greeting in capitals."), cli.option("--times", "Repeat the greeting.", cli.argument("N", "unsigned", { minimum: 1, maximum: 10 }), { default: "1" }),
      cli.flag("--trace", "Report the parsed input on stderr; diagnostics.", { hidden: true })],
    examples: [cli.example("greet Ada", "Greet Ada."), cli.example("greet Ada --shout --times 2", "Greet her twice, in capitals.")], schema: schema("greet")
  }),
  cli.read("limits", "limits", "Echo typed option values.", i => ({ data: {
    memory: i.option("--memory"), wallTimeMs: i.option("--wall-time"), level: i.option("--level"), offset: i.option("--offset"),
    digest: i.option("--digest"), tags: i.option("--tag", []), strict: i.flag("--strict"), lenient: i.flag("--lenient")
  } }), {
    options: [
      cli.option("--memory", "Memory ceiling; K, M, G and T are powers of 1024.", cli.argument("BYTES", "size", { minimum: 1 }), { default: "1G" }),
      cli.option("--wall-time", "Wall time with its unit.", cli.argument("DURATION", "duration"), { default: "10m" }),
      cli.option("--level", "Level of detail.", cli.argument("VALUE", "choice", { choices: ["low", "high"] }), { default: "low" }),
      cli.option("--offset", "Signed offset.", cli.argument("N", "integer", { minimum: -100, maximum: 100 }), { default: "0" }),
      cli.option("--digest", "Expected digest.", cli.argument("HEX", "hex", { digits: 64 })),
      cli.option("--tag", "Attach a label; may be repeated.", cli.argument("TEXT"), { repeatable: true }),
      cli.flag("--strict", "Use strict mode.", { requires: ["--level"] }), cli.flag("--lenient", "Use lenient mode.", { conflictsWith: ["--strict"] })
    ], schema: schema("limits")
  }),
  cli.transaction("note.write", "note write", "Store a note in a file.", i => {
    const file = String(i.operands[0]), content = String(i.option("--content")), replace = i.flag("--replace");
    if (existsSync(file) && !replace) throw new cli.Failure("PRECONDITION_FAILED", file + " already exists.", "Add --replace to replace it, or choose another file.");
    const fingerprint = new cli.Fingerprint().add("path", file).add("content", content).add("replace", replace ? "true" : "false").addFile("target", file, 1 << 20).finish();
    i.expect(fingerprint);
    const data = { mode: i.apply ? "apply" : "plan", path: file, bytes: Buffer.byteLength(content), action: existsSync(file) ? "replace" : "create", fingerprint };
    if (i.apply) cli.writeFileAtomic(file, Buffer.from(content), 0o644, replace ? "replace" : "no-replace");
    return { data };
  }, { operands: [cli.operand("FILE", "Destination file.")], options: [
    cli.option("--content", "Text to store.", cli.argument("TEXT"), { required: true }),
    cli.flag("--replace", "Replace an existing file.")
  ], expect: true, schema: schema("note-write") }),
  cli.records("list", "list", "List sample records.", i => {
    const records = [{ name: "alpha", size: 1 }, { name: "beta", size: 2 }, { name: "gamma", size: 3 }].slice(0, Number(i.option("--limit")));
    return { data: { count: records.length, records } };
  }, { options: [cli.option("--limit", "Maximum number of records.", cli.argument("N", "unsigned", { minimum: 0, maximum: 1000 }), { default: "5" })], schema: schema("list") }),
  cli.read("check", "check", "Check that every word is lowercase; exit 2 lists the others.", i => {
    const violations = i.operands.filter(word => word !== String(word).toLowerCase()).map(word => ({ word, reason: "not lowercase" }));
    return { data: { valid: !violations.length, violations }, exitCode: violations.length ? 2 : 0 };
  }, { operands: [cli.operand("WORDS", "Words to check.", { variadic: true })], schema: schema("check") })
]);
if (process.argv[1] && new URL("file://" + process.argv[1]).href === import.meta.url) process.exitCode = await PROGRAM.main();
