#!/usr/bin/env node
// SPDX-License-Identifier: MPL-2.0
// tests/patterns.tsv read by V8, the ECMA-262 side of the subset an
// argument.pattern is written in (agent-cli/v2 2.3): every pattern the
// subset admits compiles as an ECMA-262 regular expression (flag u, which
// refuses what the subset leaves out at its strictest), and every match
// line gets the answer the corpus writes, the one src/pattern.c and Python
// give. The libc stands for POSIX ERE in tests/test_pattern.c and RE2 in
// `make fuzz`; this is the dialect an agent's JavaScript reads.
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
  const value = fields[2].replace(/\\n|\\\\/g, (escape) => (escape === '\\n' ? '\n' : '\\'));
  // ECMA-262's `.` also leaves out \r, U+2028 and U+2029, which the
  // subset's leaves in: a value holding one is no question for V8.
  if (/[\r\u2028\u2029]/.test(value)) {
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
