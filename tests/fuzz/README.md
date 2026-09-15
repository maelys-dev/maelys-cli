# Fuzzing

Four harnesses, each an `LLVMFuzzerTestOneInput`, over the code of this
repository that reads what another program wrote:

| Harness | Target | Input |
| --- | --- | --- |
| `fuzz_run.c` | `maelys_cli_run` on `tests/catalog_surface.c`, the catalog that declares every form: parsing, help, `describe`, completion, `__complete`, rendering | argv words separated by NUL bytes |
| `fuzz_values.c` | every parser of `values.h`; an accepted value lies within the bounds given | selector byte, two 8-byte bounds, text |
| `fuzz_json.c` | `maelys_cli_json_validate`, `maelys_cli_json_format`, and the writer, whose output must validate whatever string it was handed | selector byte, bytes |
| `fuzz_manifest.c` | `maelys_cli_extension_load`: what the framework does with the document maelys-json parsed | the manifest bytes |

`make fuzz-smoke` builds each harness with `replay.h` as a plain program and
replays `corpus/NAME/`, every truncation and single-byte mutation of each
file included. It needs no libFuzzer, runs in `make check` on every leg and
under the sanitizers of `make asan-ubsan`, and never writes into the corpus.

`make fuzz` builds the same harnesses with `-fsanitize=fuzzer,address,undefined`
and runs each for `FUZZ_TIME` seconds (30 by default) from a copy of the
corpus under `build/fuzz/corpus/`; a crash is written under `build/fuzz/`.
The CI fuzz job runs it bounded. Apple clang ships no libFuzzer: on macOS,
`make fuzz FUZZ_CC=/opt/homebrew/opt/llvm/bin/clang`. A campaign that must
outlive a pull request is run where someone can watch it, never scheduled.

An input worth keeping — a crash once fixed, or a seed that reaches code the
corpus did not — is added to `corpus/NAME/` with a name that says what it
exercises. The seeds are generated test data under this repository's
license.
