#!/usr/bin/env node
// SPDX-License-Identifier: MPL-2.0
// tests/patterns.tsv read by V8, the ECMA-262 side of the subset an
// argument.pattern is written in (agent-cli/v2, section 3): every pattern
// the subset admits compiles as an ECMA-262 regular expression with the u
// flag, the reading the specification names, and every match line gets the
// answer the corpus writes, the one src/pattern.c and Python give, `.`
// included: the four line terminators of ECMA-262 are its exceptions. The
// libc stands for POSIX ERE in tests/test_pattern.c and RE2 in `make fuzz`;
// this is the dialect an agent's JavaScript reads.
'use strict';
const fs = require('fs');

const corpus = process.argv[2] || 'tests/patterns.tsv';
let compiled = 0;
let matches = 0;
let skipped = 0;
const failures = [];

fs.readFileSync(corpus, 'utf8').split('\n').forEach((line, index) => {
  if (!line || line.startsWith('#')) return;
  const fields = line.split('\t');
  const where = `${corpus}:${index + 1}`;
  if (fields[0] === 'check') {
    // A refused pattern says nothing of ECMA-262: the subset is stricter
    // than either dialect.
    if (fields[2] !== 'ok') return;
    compiled++;
    try {
      new RegExp(fields[1], 'u');
    } catch (error) {
      failures.push(`${where}: ECMA-262 refuses '${fields[1]}': ${error.message}`);
    }
    return;
  }
  if (fields[0] !== 'match') {
    failures.push(`${where}: malformed line`);
    return;
  }
  // \xHH is a byte, which a JavaScript string cannot hold: such a value is
  // C's and Python's question, not V8's.
  if (/\\x[0-9a-fA-F]{2}/.test(fields[2])) {
    skipped++;
    return;
  }
  const value = fields[2].replace(/\\(n|r|\\|u[0-9a-fA-F]{4})/g, (escape, code) =>
    code === 'n' ? '\n' : code === 'r' ? '\r' : code === '\\' ? '\\' : String.fromCharCode(parseInt(code.slice(1), 16)));
  // A byte that is not UTF-8 has no code point in a JavaScript string: such
  // a value is C's and Python's question, not V8's.
  if (/[\ud800-\udfff]/.test(value)) {
    skipped++;
    return;
  }
  matches++;
  let expression;
  try {
    expression = new RegExp(fields[1], 'u');
  } catch (error) {
    failures.push(`${where}: ECMA-262 refuses '${fields[1]}': ${error.message}`);
    return;
  }
  const got = expression.test(value);
  if (got !== (fields[3] === 'yes')) {
    failures.push(`${where}: V8 reads '${fields[1]}' on ${JSON.stringify(value)} as ${got ? 'yes' : 'no'}`);
  }
});

for (const failure of failures) console.error(failure);
if (compiled < 30 || matches < 80) failures.push('the corpus is shorter than expected');
console.log(`pattern-ecma-check: ${failures.length ? 'FAILED' : 'ok'} ` +
  `(V8 ${process.versions.v8}: ${compiled} patterns compile, ${matches} matches, ${skipped} skipped)`);
process.exit(failures.length ? 1 : 0);
