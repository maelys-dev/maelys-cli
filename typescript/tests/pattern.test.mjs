// SPDX-License-Identifier: MPL-2.0
import { readFileSync } from 'node:fs';
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { Regex, RegexError } from '../dist/index.js';
const lines = readFileSync(new URL('../../c/tests/patterns.tsv', import.meta.url), 'utf8').split('\n');
const decode = value => {
  const bytes = [];
  const parts = value.split(/(\\(?:n|r|\\|u[0-9a-fA-F]{4}|x[0-9a-fA-F]{2}))/);
  for (const part of parts) {
    if (/^\\x[0-9a-fA-F]{2}$/.test(part)) bytes.push(Number.parseInt(part.slice(2), 16));
    else if (/^\\u[0-9a-fA-F]{4}$/.test(part)) {
      const code = Number.parseInt(part.slice(2), 16);
      // The corpus encodes lone surrogate values as UTF-8 bytes, as the C fixture does.
      if (code >= 0xd800 && code <= 0xdfff) bytes.push(0xe0 | code >> 12, 0x80 | code >> 6 & 63, 0x80 | code & 63);
      else bytes.push(...Buffer.from(String.fromCharCode(code)));
    } else bytes.push(...Buffer.from(({ '\\n': '\n', '\\r': '\r', '\\\\': '\\' })[part] ?? part));
  }
  return Buffer.from(bytes);
};
for (const [i, line] of lines.entries()) {
  if (!line || line.startsWith('#')) continue;
  const [kind, pattern, value, expected, offset] = line.split('\t');
  test('shared pattern corpus line ' + (i + 1), () => {
    if (kind === 'check' && value === 'ok') assert.ok(new Regex(pattern));
    else if (kind === 'check') assert.throws(() => new Regex(pattern), error => error instanceof RegexError && error.code === expected && error.offset === Number(offset));
    else assert.equal(new Regex(pattern).search(decode(value)), expected === 'yes');
  });
}
test('nested repetition remains bounded without a backtracking engine', () => {
  const regex = new Regex('^(a*)*b$');
  assert.equal(regex.search('a'.repeat(100000)), false);
  assert.ok(regex.states < 20);
});

test('generation wrap clears visited states before reusing a generation', () => {
  // Exercise the private matching engine near uint32 wrap, without a 4 GiB input.
  for (const [pattern, input, expected] of [['^(a*)*b$', 'aaaaab', true], ['^(a*)*b$', 'aaaaa', false], ['^a*$', 'aaaaa', true]]) {
    const regex = new Regex(pattern);
    assert.equal(regex.run(input, 0xfffffffe), expected);
    assert.equal(regex.search(input), expected);
  }
});
