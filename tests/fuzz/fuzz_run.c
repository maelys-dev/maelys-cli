/* SPDX-License-Identifier: MPL-2.0 */
/* maelys_cli_run on words an agent could type, against the catalog that
 * declares every form the framework can serialize (tests/catalog_surface.c):
 * command resolution, option spelling and values of every kind, groups and
 * constraints, operands, rendering options, and the built-ins help,
 * describe, completion and __complete that read the same words.
 *
 * Input: argv without the program name, words separated by NUL bytes.
 * Output goes to /dev/null; a failure is a sanitizer report or a crash,
 * never an exit code, which every refusal legitimately sets. */
#define CATALOG_SURFACE_NO_MAIN
#include "../catalog_surface.c"
#include "replay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RUN_MAX_BYTES 4096u
#define RUN_MAX_WORDS 64

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static FILE *sink;
    if (!sink) {
        /* The environment the runtime reads is fixed once: a format or a
         * colour decided by the machine running the fuzzer would make an
         * input behave differently from one run to the next. */
        (void)unsetenv("MAELYS_CLI_FORMAT");
        (void)unsetenv("CLICOLOR_FORCE");
        (void)unsetenv("COLUMNS");
        (void)setenv("NO_COLOR", "1", 1);
        sink = fopen("/dev/null", "w");
        if (!sink) abort();
    }
    if (size > RUN_MAX_BYTES) return 0;
    char *copy = malloc(size + 1u);
    if (!copy) return 0;
    memcpy(copy, data, size);
    copy[size] = '\0';
    char *words[RUN_MAX_WORDS + 1];
    int count = 0;
    for (char *word = copy; word < copy + size && count < RUN_MAX_WORDS;
         word += strlen(word) + 1u)
        words[count++] = word;
    words[count] = NULL;
    (void)maelys_cli_run(&surface_app, count, words, sink, sink);
    free(copy);
    return 0;
}
