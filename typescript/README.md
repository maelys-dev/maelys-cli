# @maelys-dev/cli

Native Node implementation of the Maelys agent-cli/v2 runtime, for both
TypeScript and JavaScript. ESM, matching declarations, Node >=22.13.0,
Linux/macOS, no runtime dependency.

Development candidate: the existing v0.13.0 tag does not contain this
implementation. A local archive is not a published release.

```js
import { Program, read, operand } from '@maelys-dev/cli';
const cli = new Program('sample', 'Sample', '1.0.0', [
  read('show', 'show', 'Show a value.',
    i => ({ data: { value: i.operands[0] } }),
    { operands: [operand('VALUE', 'Value to show.')] })
]);
process.exitCode = await cli.main();
```

One catalog supplies parsing, help, describe, completion and structured
replies. Transactions plan by default and write with --apply.
See https://github.com/maelys-dev/maelys-cli/blob/main/docs/typescript.md
for the declaration vocabulary, supported surface and verification.
