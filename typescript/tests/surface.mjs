#!/usr/bin/env node
// SPDX-License-Identifier: MPL-2.0
// Exactly the C fixture declarations, exercising the independent Node parser.
import { readFileSync, appendFileSync } from 'node:fs';
import * as cli from '../dist/index.js';
const isDefault = process.argv[2] === '--default-fixture';
if (isDefault) process.argv.splice(2, 1);
const commands = JSON.parse(readFileSync(new URL('../../conformance/fixtures/' + (isDefault ? 'default_surface' : 'catalog_surface') + '.json', import.meta.url)));
for (const command of commands) command.handler = i => {
  if (isDefault && process.env.DEFAULT_SURFACE_TRACE) appendFileSync(process.env.DEFAULT_SURFACE_TRACE, command.id + '\n');
  if (isDefault) {
    if (command.id === 'show') {
      const label = String(i.option('--label', ''));
      if (label.startsWith('!')) throw new cli.Failure('PRECONDITION_FAILED', 'Label ' + label + ' is reserved.', "Choose a label that does not start with '!'.");
      return { data: { label, level: i.option('--level', ''), file: i.operands[0] ?? '' } };
    }
    if (command.id === 'list') { const value = i.option('--limit'); return { data: { limit: value === null ? '' : String(value) } }; }
    if (command.id === 'save') return { data: { mode: i.apply ? 'apply' : 'plan', changed: false } };
    process.stdout.write('watching\n'); return 0;
  }
  if (command.id === 'refuse') throw new cli.Failure('VALIDATION_FAILED', 'The surface configuration is refused.', 'Fix the surface configuration.',
    [{ code: 'SURFACE_KEY', path: ['surface', 'keys', 2], message: "Unknown key 'colour'." }], { 'x-surface-revision': 7 });
  if (command.id === 'bound') {
    const fingerprint = new cli.Fingerprint().add('surface', 'bound').finish(); i.expect(fingerprint);
    return { data: { mode: i.apply ? 'apply' : 'plan', fingerprint } };
  }
  if (command.outputMode === 'protocol-stream') return 0;
  if (command.outputMode === 'json-records') return { data: { count: 0, records: [] } };
  return { data: {} };
};
const PROGRAM = new cli.Program(isDefault ? 'default-surface' : 'catalog-surface', isDefault ? 'Default surface' : 'Catalog surface', '1.0.0', commands,
  isDefault ? { defaultCommand: process.env.DEFAULT_SURFACE_DEFAULT === 'none' ? undefined : process.env.DEFAULT_SURFACE_DEFAULT || 'show' } : {});
process.exitCode = await PROGRAM.main();
