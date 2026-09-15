/* SPDX-License-Identifier: MPL-2.0 */
/* The replay driver every harness of this directory shares.
 *
 * Built with -DMAELYS_CLI_FUZZ_REPLAY, a harness is an ordinary program
 * that runs LLVMFuzzerTestOneInput on each file of the directories it is
 * given, then on every truncation of that file and on every single-byte
 * mutation of it, each list bounded. It needs no libFuzzer, so
 * `make fuzz-smoke` runs under any C compiler, on macOS as on Linux, and
 * under the sanitizers of `make asan-ubsan`. Built with
 * -fsanitize=fuzzer instead, the same harness is the fuzzer of `make fuzz`,
 * and this file adds nothing.
 *
 * The corpus is only read: a replay that wrote into it would turn a
 * regression test into a moving target. */
#ifndef MAELYS_CLI_FUZZ_REPLAY_H
#define MAELYS_CLI_FUZZ_REPLAY_H

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

#ifdef MAELYS_CLI_FUZZ_REPLAY

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REPLAY_MAX_BYTES 65536u
#define REPLAY_MAX_VARIANTS 256u

static const unsigned char replay_mutations[] = {0x00, 0xff, '-', '=', '"'};

static void replay_one(const uint8_t *bytes, size_t size) {
    (void)LLVMFuzzerTestOneInput(bytes, size);
    size_t step = size / REPLAY_MAX_VARIANTS + 1u;
    for (size_t cut = 0u; cut < size; cut += step)
        (void)LLVMFuzzerTestOneInput(bytes, cut);
    uint8_t *mutated = malloc(size ? size : 1u);
    if (!mutated) return;
    for (size_t at = 0u; at < size; at += step) {
        for (size_t m = 0u; m < sizeof(replay_mutations); ++m) {
            memcpy(mutated, bytes, size);
            mutated[at] = replay_mutations[m];
            (void)LLVMFuzzerTestOneInput(mutated, size);
        }
    }
    free(mutated);
}

static int replay_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        perror(path);
        return -1;
    }
    uint8_t *bytes = malloc(REPLAY_MAX_BYTES);
    size_t size = bytes ? fread(bytes, 1u, REPLAY_MAX_BYTES, file) : 0u;
    int failed = !bytes || ferror(file);
    (void)fclose(file);
    if (!failed) replay_one(bytes, size);
    free(bytes);
    return failed ? -1 : 0;
}

static int compare_names(const void *left, const void *right) {
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

/* Replays the files of each directory in lexical order, so a failure
 * names the same input on every machine. */
static int replay_directory(const char *directory, size_t *replayed) {
    DIR *handle = opendir(directory);
    if (!handle) {
        perror(directory);
        return -1;
    }
    char **names = NULL;
    size_t count = 0u;
    int failed = 0;
    struct dirent *entry;
    while ((entry = readdir(handle)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char **grown = realloc(names, (count + 1u) * sizeof(*names));
        size_t length = strlen(directory) + strlen(entry->d_name) + 2u;
        char *path = grown ? malloc(length) : NULL;
        if (!path) {
            if (grown) names = grown;
            failed = 1;
            break;
        }
        names = grown;
        (void)snprintf(path, length, "%s/%s", directory, entry->d_name);
        names[count++] = path;
    }
    (void)closedir(handle);
    if (count) qsort(names, count, sizeof(*names), compare_names);
    for (size_t i = 0u; i < count; ++i) {
        if (!failed && replay_file(names[i]) != 0) failed = 1;
        free(names[i]);
    }
    free(names);
    *replayed += count;
    return failed ? -1 : 0;
}

int main(int argc, char **argv) {
    size_t replayed = 0u;
    if (argc < 2) {
        (void)fprintf(stderr, "usage: %s CORPUS_DIRECTORY...\n", argv[0]);
        return 2;
    }
    for (int i = 1; i < argc; ++i)
        if (replay_directory(argv[i], &replayed) != 0) return 1;
    if (replayed == 0u) {
        (void)fprintf(stderr, "%s: no input in the corpus\n", argv[0]);
        return 1;
    }
    (void)printf("%s: %zu inputs replayed\n", argv[0], replayed);
    return 0;
}

#endif
#endif
