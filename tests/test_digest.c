#include "check.h"

#include <maelys/cli/digest.h>
#include <maelys/cli/files.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int test_vectors(void) {
    char hex[MAELYS_CLI_SHA256_HEX_SIZE];
    maelys_cli_sha256_hex("", 0u, hex);
    CHECK(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
    maelys_cli_sha256_hex("abc", 3u, hex);
    CHECK(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    const char *long_input = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    maelys_cli_sha256_hex(long_input, strlen(long_input), hex);
    CHECK(strcmp(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0);
    /* One million 'a' through incremental updates. */
    maelys_cli_sha256_t context;
    unsigned char digest[MAELYS_CLI_SHA256_SIZE];
    maelys_cli_sha256_init(&context);
    char chunk[1000];
    memset(chunk, 'a', sizeof(chunk));
    for (int i = 0; i < 1000; ++i) maelys_cli_sha256_update(&context, chunk, sizeof(chunk));
    maelys_cli_sha256_final(&context, digest);
    static const unsigned char expected[] = {
        0xcd, 0xc7, 0x6e, 0x5c, 0x99, 0x14, 0xfb, 0x92, 0x81, 0xa1, 0xc7, 0xe2,
        0x84, 0xd7, 0x3e, 0x67, 0xf1, 0x80, 0x9a, 0x48, 0xa4, 0x97, 0x20, 0x0e,
        0x04, 0x6d, 0x39, 0xcc, 0xc7, 0x11, 0x2c, 0xd0
    };
    CHECK(memcmp(digest, expected, sizeof(expected)) == 0);
    return 1;
}

static int test_file(void) {
    char path[] = "/tmp/maelys-cli-digest.XXXXXX";
    int descriptor = mkstemp(path);
    CHECK(descriptor >= 0);
    CHECK(write(descriptor, "abc", 3u) == 3);
    CHECK(close(descriptor) == 0);
    char hex[MAELYS_CLI_SHA256_HEX_SIZE];
    CHECK(maelys_cli_sha256_file(path, 16u, hex) == 0);
    CHECK(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    CHECK(maelys_cli_sha256_file(path, 2u, hex) != 0);
    (void)unlink(path);
    return 1;
}

/* The fingerprint of a plan. The expected strings were computed apart from
 * this library, from the framing digest.h states: label and value preceded
 * by their length on eight bytes, a presence byte between them. */
static int fingerprint_of(const char *label, const void *bytes, size_t size,
                          const char *expected) {
    maelys_cli_fingerprint_t fingerprint;
    char out[MAELYS_CLI_FINGERPRINT_SIZE];
    maelys_cli_fingerprint_init(&fingerprint);
    maelys_cli_fingerprint_add(&fingerprint, label, bytes, size);
    maelys_cli_fingerprint_finish(&fingerprint, out);
    return strcmp(out, expected) == 0;
}

static int test_fingerprint(void) {
    maelys_cli_fingerprint_t fingerprint;
    char out[MAELYS_CLI_FINGERPRINT_SIZE];
    maelys_cli_fingerprint_init(&fingerprint);
    maelys_cli_fingerprint_finish(&fingerprint, out);
    CHECK(strcmp(out, "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
    CHECK(strlen(out) + 1u == MAELYS_CLI_FINGERPRINT_SIZE);
    /* ("ab", "c") and ("a", "bc") are two entries, not one concatenation. */
    CHECK(fingerprint_of("ab", "c", 1u,
        "sha256:98567ed2582c877b4f71760c31e078ab7d7bd86f42689d58325689d652f4056a"));
    CHECK(fingerprint_of("a", "bc", 2u,
        "sha256:890cb9913f8086a050b29183b982ee91719b35a2f82cfb9b00a01d6c9884031d"));
    /* An absent value is not an empty one. */
    CHECK(fingerprint_of("k", NULL, 0u,
        "sha256:d815ac6f89858e27e4a82ec5e72a64929b510196736898158ab4b48c00410c3d"));
    CHECK(fingerprint_of("k", "", 0u,
        "sha256:629d67d8c50c9d34279f5c01951a2ccdbe46930ac5da8e5b9ac52205f60a0a45"));
    maelys_cli_fingerprint_init(&fingerprint);
    maelys_cli_fingerprint_add_string(&fingerprint, "path", "/tmp/x");
    maelys_cli_fingerprint_add_string(&fingerprint, "content", "hi");
    maelys_cli_fingerprint_finish(&fingerprint, out);
    CHECK(strcmp(out, "sha256:aeff1db4e0dd584988c9a8a30434603418a6ba8813483d00ef7227191cea74a0") == 0);

    /* The state of a file: absent, its content, and nothing when it cannot
     * be read as one. */
    char directory[] = "/tmp/maelys-cli-fingerprint.XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    char path[512];
    (void)snprintf(path, sizeof(path), "%s/note", directory);
    maelys_cli_fingerprint_init(&fingerprint);
    CHECK(maelys_cli_fingerprint_add_file(&fingerprint, "k", path, 16u) == 0);
    maelys_cli_fingerprint_finish(&fingerprint, out);
    CHECK(strcmp(out, "sha256:d815ac6f89858e27e4a82ec5e72a64929b510196736898158ab4b48c00410c3d") == 0);
    FILE *file = fopen(path, "w");
    CHECK(file && fputs("hi", file) != EOF && fclose(file) == 0);
    maelys_cli_fingerprint_init(&fingerprint);
    CHECK(maelys_cli_fingerprint_add_file(&fingerprint, "target", path, 16u) == 0);
    maelys_cli_fingerprint_finish(&fingerprint, out);
    CHECK(strcmp(out, "sha256:a20aaeef176c63d62a064d2db7831d3b1cce5a3e99b5369f0ce16dc985f51fbd") == 0);
    maelys_cli_fingerprint_init(&fingerprint);
    CHECK(maelys_cli_fingerprint_add_file(&fingerprint, "target", path, 1u) != 0);  /* too large */
    CHECK(maelys_cli_fingerprint_add_file(&fingerprint, "target", directory, 16u) != 0); /* not a file */
    (void)unlink(path);
    (void)rmdir(directory);
    return 1;
}

int main(void) {
    int failures = 0;
    RUN(test_vectors);
    RUN(test_file);
    RUN(test_fingerprint);
    return failures ? 1 : 0;
}
