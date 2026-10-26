// SPDX-License-Identifier: MPL-2.0
// An installed consumer outside the source tree needs only Node and the tgz.
import { execFileSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, writeFileSync, rmSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const scratch = mkdtempSync(path.join(tmpdir(), 'maelys-cli-npm-'));
const env = { ...process.env, npm_config_cache: path.join(scratch, 'cache') };
try {
  const packed = JSON.parse(execFileSync('npm', ['pack', '--ignore-scripts', '--json', '--pack-destination', scratch], { cwd: path.join(root, 'typescript'), encoding: 'utf8', env }))[0];
  const consumer = path.join(scratch, 'consumer');
  mkdirSync(consumer);
  writeFileSync(path.join(consumer, 'package.json'), '{"type":"module","private":true}\n');
  execFileSync('npm', ['install', '--offline', '--ignore-scripts', '--no-audit', '--no-fund', path.join(scratch, packed.filename)], { cwd: consumer, stdio: 'pipe', env });
  writeFileSync(path.join(consumer, 'main.mjs'), [
    "import { Program, read, operand } from '@maelys-dev/cli';",
    "const cli = new Program('installed-sample', 'Installed sample', '1.0.0', [",
    "  read('show', 'show', 'Show a value.', i => ({ data: { value: i.operands[0] } }),",
    "    { operands: [operand('VALUE', 'Value to show.')] })",
    "]);",
    "process.exitCode = await cli.main();"
  ].join('\n') + '\n');
  const result = JSON.parse(execFileSync(process.execPath, ['main.mjs', 'show', 'portable', '--format', 'json'], { cwd: consumer, encoding: 'utf8' }));
  if (result.data.value !== 'portable' || result.contract !== 'agent-cli/v2') throw new Error('Installed consumer reply differs.');
  const manifest = JSON.parse(readFileSync(path.join(consumer, 'node_modules/@maelys-dev/cli/package.json')));
  if (Object.keys(manifest.dependencies ?? {}).length) throw new Error('Runtime dependency closure is not empty.');
  // TypeScript itself and its standard Node typings are development tools;
  // the installed package supplies all of its own declaration imports.
  writeFileSync(path.join(consumer, 'main.ts'), readFileSync(path.join(consumer, 'main.mjs')));
  execFileSync(process.execPath, [path.join(root, 'typescript/node_modules/typescript/bin/tsc'),
    '--strict', '--noEmit', '--target', 'ES2022', '--module', 'NodeNext', '--moduleResolution', 'NodeNext',
    '--typeRoots', path.join(root, 'typescript/node_modules/@types'), 'main.ts'], { cwd: consumer, stdio: 'pipe' });
  console.log('typescript-package-check: ok (offline install, external JS/TS consumers, no C/Python/runtime dependency)');
} finally { rmSync(scratch, { recursive: true, force: true }); }
