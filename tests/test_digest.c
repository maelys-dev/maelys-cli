#include "check.h"

#include <maelys/cli/digest.h>
#include <maelys/cli/files.h>

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

int main(void) {
    int failures = 0;
    RUN(test_vectors);
    RUN(test_file);
    return failures ? 1 : 0;
}
