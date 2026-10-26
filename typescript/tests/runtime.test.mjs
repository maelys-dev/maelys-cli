// SPDX-License-Identifier: MPL-2.0
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, writeFileSync, existsSync, symlinkSync, rmSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import * as cli from '../dist/index.js';
const hello = fileURLToPath(new URL('../examples/hello.mjs', import.meta.url));
const invoke = (args, env = {}) => {
  const result = spawnSync(process.execPath, [hello, ...args], { encoding: 'utf8', env: { ...process.env, MAELYS_CLI_FORMAT: '', MAELYS_CLI_LANG: '', LC_ALL: 'C.UTF-8', ...env } });
  assert.ifError(result.error); return result;
};
test('64-bit integers are parsed and serialized exactly, and overflow is refused', () => {
  const maximum = cli.parseValue(cli.argument('N', 'unsigned'), '18446744073709551615', 'N', 'read');
  assert.equal(maximum, 18446744073709551615n);
  assert.equal(cli.stringify({ value: maximum }), '{"value":18446744073709551615}');
  assert.throws(() => cli.parseValue(cli.argument('N', 'unsigned'), '18446744073709551616', 'N', 'read'), cli.Failure);
  assert.throws(() => cli.parseValue(cli.argument('N', 'integer'), '-9223372036854775809', 'N', 'read'), cli.Failure);
});
test('value grammars never accept trailing newlines or whitespace', () => {
  for (const [kind, value, bounds] of [['unsigned', '1', {}], ['integer', '-1', {}], ['size', '1G', {}],
    ['duration', '1s', {}], ['hex', 'ab', { digits: 2 }], ['sha256', 'a'.repeat(64), {}],
    ['digest', 'sha256:' + 'a'.repeat(64), { algorithms: ['sha256'] }]]) {
    for (const suffix of ['\n', '\r', ' ', '\u2028']) assert.throws(() => cli.parseValue(cli.argument('N', kind, bounds), value + suffix, 'N', 'read'));
  }
  assert.throws(() => new cli.Program('unsafe\n', 'Product', '1', []));
  assert.throws(() => cli.read('unsafe\n', 'unsafe', 'Purpose', () => ({ data: {} })));
});
test('field text uses code-point sorted columns and escaped values', () => {
  assert.equal(cli.recordText([{ '😀': 'astral', '\uffff': 'BMP' }]), 'BMP\tastral\n');
  assert.equal(cli.fieldText('one\ntwo\tthree\\'), 'one\\ntwo\\tthree\\\\\n');
});
test('malformed flags fail before help and no product handler runs for help', () => {
  const failure = invoke(['greet', '--times', 'bad', '--help', '--json']);
  assert.equal(failure.status, 1);
  assert.equal(failure.stdout, '');
  assert.equal(JSON.parse(failure.stderr).error.code, 'VALIDATION_FAILED');
  const help = invoke(['greet', '--help', '--json']);
  assert.equal(help.status, 0);
  assert.equal(JSON.parse(help.stdout).command, 'help');
});
test('unknown command wins over unknown options; repeated and foreign options fail', () => {
  for (const args of [['none', '--bogus'], ['greet', 'Ada', '--shout', '--shout'], ['greet', 'Ada', '--limit', '2']]) {
    const result = invoke([...args, '--json']);
    assert.equal(result.status, 1); assert.equal(result.stdout, '');
    assert.equal(JSON.parse(result.stderr).error.code, args[0] === 'none' ? 'INVALID_COMMAND' : 'VALIDATION_FAILED');
  }
});
test('transactions plan, bind state and refuse invalid fields before writing', () => {
  const directory = mkdtempSync(path.join(os.tmpdir(), 'maelys-ts-transaction-'));
  try {
    const file = path.join(directory, 'note');
    const args = ['note', 'write', file, '--content', 'hello'];
    const plan = invoke([...args, '--json']);
    assert.equal(plan.status, 0); assert.equal(existsSync(file), false);
    const fingerprint = JSON.parse(plan.stdout).data.fingerprint;
    const stale = invoke([...args, '--apply', '--expect', 'sha256:' + '0'.repeat(64), '--json']);
    assert.equal(stale.status, 1); assert.equal(JSON.parse(stale.stderr).error.code, 'PRECONDITION_FAILED'); assert.equal(existsSync(file), false);
    for (const extra of [['--field', 'unrequired'], ['--field', 'path', '--json']]) {
      const invalid = invoke([...args, '--apply', ...extra], { MAELYS_CLI_FORMAT: extra.includes('--json') ? '' : 'json' });
      assert.equal(invalid.status, 1); assert.equal(existsSync(file), false);
    }
    const applied = invoke([...args, '--apply', '--expect', fingerprint, '--json']);
    assert.equal(applied.status, 0); assert.equal(readFileSync(file, 'utf8'), 'hello');
    assert.equal(JSON.parse(applied.stdout).data.fingerprint, fingerprint);
  } finally { rmSync(directory, { recursive: true, force: true }); }
});
test('atomic no-replace never overwrites; bounded reads refuse links and oversized files', () => {
  const directory = mkdtempSync(path.join(os.tmpdir(), 'maelys-ts-file-'));
  try {
    const file = path.join(directory, 'value');
    cli.writeFileAtomic(file, Buffer.from('original'), 0o600, 'no-replace');
    assert.throws(() => cli.writeFileAtomic(file, Buffer.from('changed'), 0o600, 'no-replace'));
    assert.equal(readFileSync(file, 'utf8'), 'original');
    assert.throws(() => cli.readRegularFile(file, 0, 3));
    symlinkSync(file, path.join(directory, 'link'));
    assert.throws(() => cli.readRegularFile(path.join(directory, 'link'), 0, 100));
  } finally { rmSync(directory, { recursive: true, force: true }); }
});
test('catalog refuses duplicate identifiers, malformed examples, hidden defaults and invalid constraints', () => {
  const handler = () => ({ data: {} });
  assert.throws(() => new cli.Program('sample', 'Sample', '1', [cli.read('a', 'a', 'a', handler), cli.read('a', 'b', 'b', handler)]));
  assert.throws(() => new cli.Program('sample', 'Sample', '1', [cli.read('a', 'a', 'a', handler, { hidden: true })], { defaultCommand: 'a' }));
  assert.throws(() => cli.constraint('exactly-one', '--a', '--a'));
  assert.throws(() => cli.read('a', 'a', 'a', handler, { options: [cli.flag('--a', 'a', { requires: ['--missing'] })] }));
  assert.throws(() => new cli.Program('sample', 'Sample', '1', [cli.read('a', 'a', 'a', handler, { examples: [cli.example('a --unknown', 'bad')] })]));
});
test('machine output is invariant under locale and language options', () => {
  const args = ['describe', '--json', '--compact'];
  const reference = invoke(args).stdout;
  for (const tag of ['fr', 'FR-ca', 'de', 'zz']) assert.equal(invoke([...args, '--lang', tag], { LC_ALL: 'fr_FR.UTF-8' }).stdout, reference);
});

const invokeFailure = (expression, args = ['--json'], translation = 'Missing %s.') => {
  const moduleUrl = new URL('../dist/index.js', import.meta.url).href;
  const source = `import * as cli from ${JSON.stringify(moduleUrl)};
    const program = new cli.Program('sample', 'Sample', '1', [
      cli.read('show', 'show', 'Show.', () => { throw (${expression}); })
    ], { translations: { fr: { 'Missing %s.': ${JSON.stringify(translation)} } } });
    process.exitCode = await program.main(['show', ...${JSON.stringify(args)}]);`;
  return spawnSync(process.execPath, ['--input-type=module', '-e', source], {
    encoding: 'utf8', env: { ...process.env, MAELYS_CLI_FORMAT: '', MAELYS_CLI_LANG: '', LC_ALL: 'C' }
  });
};
test('templated failures translate human output and preserve canonical JSON', () => {
  const expression = "cli.failure('NOT_FOUND', 'Missing %s.', ['note'])";
  const json = invokeFailure(expression, ['--json', '--lang', 'fr'], 'Absent : %s.');
  assert.equal(json.status, 1); assert.equal(json.stdout, '');
  assert.equal(JSON.parse(json.stderr).error.message, 'Missing note.');
  const text = invokeFailure(expression, ['--lang', 'fr', '--color', 'never'], 'Absent : %s.');
  assert.equal(text.status, 1); assert.match(text.stderr, /Absent : note\./);
  const brokenTranslation = invokeFailure(expression, ['--lang', 'fr'], 'Missing %s %s.');
  assert.equal(brokenTranslation.status, 1); assert.match(brokenTranslation.stderr, /Missing note\./);
  assert.throws(() => cli.failure('NOT_FOUND', 'Missing %s.', []), TypeError);
  assert.throws(() => cli.failure('NOT_FOUND', 'Missing %08d.', [3]), TypeError);
});
test('malformed failure metadata yields one valid UNEXPECTED envelope', () => {
  const expressions = [
    "new cli.Failure('BAD\\n', 'bad')",
    "new cli.Failure('BAD', 'bad', '', [{ code: 2, message: 'bad' }])",
    "new cli.Failure('BAD', 'bad', '', undefined, { foreign: true })",
    "new cli.Failure('BAD', 'bad', '', undefined, { 'x-number': Infinity })",
    "(() => { const value = {}; value.self = value; return new cli.Failure('BAD', 'bad', '', undefined, { 'x-cycle': value }); })()",
    "Object.assign(new cli.Failure('BAD', 'bad'), { template: '%s', templateArguments: [] })"
  ];
  for (const expression of expressions) {
    const result = invokeFailure(expression);
    assert.equal(result.status, 1); assert.equal(result.stdout, '');
    assert.equal(JSON.parse(result.stderr).error.code, 'UNEXPECTED', expression);
  }
});
