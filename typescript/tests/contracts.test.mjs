// SPDX-License-Identifier: MPL-2.0
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import ts from 'typescript';
import { pagerArguments } from '../dist/terminal.js';

const source = readFileSync(new URL('../src/index.ts', import.meta.url), 'utf8');
const ast = ts.createSourceFile('index.ts', source, ts.ScriptTarget.Latest, true);
const names = ast.statements.flatMap(statement => ts.isExportDeclaration(statement) && statement.exportClause && ts.isNamedExports(statement.exportClause)
  ? statement.exportClause.elements.map(element => element.name.text) : []);
const checkDocs = markdown => names.filter(name => !markdown.includes('`' + name + '`') && !markdown.includes('`' + name + '('));
test('every exported runtime symbol and type is documented', () => {
  assert.ok(names.length > 30);
  const markdown = readFileSync(new URL('../../docs/typescript.md', import.meta.url), 'utf8');
  assert.deepEqual(checkDocs(markdown), []);
  assert.ok(checkDocs(markdown.replaceAll('`Fingerprint`', 'Missing fingerprint')).includes('Fingerprint'));
});
test('the required aggregate CI cannot omit the Node implementation', () => {
  const text = readFileSync(new URL('../../.github/workflows/ci.yml', import.meta.url), 'utf8');
  const checked = source => /ci:\n\s+needs: \[[^\]]*\btypescript\b/.test(source) && /run: make typescript-check/.test(source);
  assert.equal(checked(text), true);
  assert.equal(checked(text.replace(', typescript]', ']')), false);
  assert.equal(checked(text.replace('run: make typescript-check', 'run: true')), false);
});
test('pager follows POSIX quoting without evaluating a shell', () => {
  assert.deepEqual(pagerArguments(undefined), ['less']);
  assert.equal(pagerArguments('  '), null);
  assert.deepEqual(pagerArguments('"/path with spaces/pager" -R'), ['/path with spaces/pager', '-R']);
  assert.deepEqual(pagerArguments("pager '$HOME' \"; touch /tmp/no\""), ['pager', '$HOME', '; touch /tmp/no']);
  assert.deepEqual(pagerArguments('pager "" a\\ b'), ['pager', '', 'a b']);
  assert.equal(pagerArguments('pager "unclosed'), null);
  assert.equal(pagerArguments('pager \\'), null);
});
