#include "maelys/cli/digest.h"
#include "maelys/cli/files.h"

#include <stdlib.h>
#include <string.h>

static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static uint32_t rotate_right(uint32_t value, unsigned int bits) {
    return (value >> bits) | (value << (32u - bits));
}

static void transform(uint32_t state[8], const unsigned char block[64]) {
    uint32_t w[64];
    for (size_t i = 0u; i < 16u; ++i) {
        w[i] = ((uint32_t)block[i * 4u] << 24) |
               ((uint32_t)block[i * 4u + 1u] << 16) |
               ((uint32_t)block[i * 4u + 2u] << 8) |
               (uint32_t)block[i * 4u + 3u];
    }
    for (size_t i = 16u; i < 64u; ++i) {
        uint32_t s0 = rotate_right(w[i - 15u], 7u) ^
            rotate_right(w[i - 15u], 18u) ^ (w[i - 15u] >> 3);
        uint32_t s1 = rotate_right(w[i - 2u], 17u) ^
            rotate_right(w[i - 2u], 19u) ^ (w[i - 2u] >> 10);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (size_t i = 0u; i < 64u; ++i) {
        uint32_t s1 = rotate_right(e, 6u) ^ rotate_right(e, 11u) ^
            rotate_right(e, 25u);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + ch + K[i] + w[i];
        uint32_t s0 = rotate_right(a, 2u) ^ rotate_right(a, 13u) ^
            rotate_right(a, 22u);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void maelys_cli_sha256_init(maelys_cli_sha256_t *context) {
    static const uint32_t initial[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    memcpy(context->state, initial, sizeof(initial));
    context->length = 0u;
    context->buffered = 0u;
}

void maelys_cli_sha256_update(
    maelys_cli_sha256_t *context, const void *bytes, size_t size) {
    const unsigned char *input = bytes;
    context->length += (uint64_t)size;
    while (size > 0u) {
        size_t space = 64u - context->buffered;
        size_t take = size < space ? size : space;
        memcpy(context->buffer + context->buffered, input, take);
        context->buffered += take;
        input += take;
        size -= take;
        if (context->buffered == 64u) {
            transform(context->state, context->buffer);
            context->buffered = 0u;
        }
    }
}

void maelys_cli_sha256_final(
    maelys_cli_sha256_t *context, unsigned char out[MAELYS_CLI_SHA256_SIZE]) {
    uint64_t bit_length = context->length * 8u;
    unsigned char pad = 0x80u;
    maelys_cli_sha256_update(context, &pad, 1u);
    unsigned char zero = 0u;
    while (context->buffered != 56u) maelys_cli_sha256_update(context, &zero, 1u);
    unsigned char length_bytes[8];
    for (size_t i = 0u; i < 8u; ++i)
        length_bytes[i] = (unsigned char)(bit_length >> (56u - 8u * i));
    maelys_cli_sha256_update(context, length_bytes, 8u);
    for (size_t i = 0u; i < 8u; ++i) {
        out[i * 4u] = (unsigned char)(context->state[i] >> 24);
        out[i * 4u + 1u] = (unsigned char)(context->state[i] >> 16);
        out[i * 4u + 2u] = (unsigned char)(context->state[i] >> 8);
        out[i * 4u + 3u] = (unsigned char)context->state[i];
    }
}

static void to_hex(
    const unsigned char digest[MAELYS_CLI_SHA256_SIZE],
    char out[MAELYS_CLI_SHA256_HEX_SIZE]) {
    static const char alphabet[] = "0123456789abcdef";
    for (size_t i = 0u; i < MAELYS_CLI_SHA256_SIZE; ++i) {
        out[i * 2u] = alphabet[digest[i] >> 4];
        out[i * 2u + 1u] = alphabet[digest[i] & 0x0fu];
    }
    out[MAELYS_CLI_SHA256_HEX_SIZE - 1u] = '\0';
}

void maelys_cli_sha256_hex(
    const void *bytes, size_t size, char out[MAELYS_CLI_SHA256_HEX_SIZE]) {
    maelys_cli_sha256_t context;
    unsigned char digest[MAELYS_CLI_SHA256_SIZE];
    maelys_cli_sha256_init(&context);
    maelys_cli_sha256_update(&context, bytes, size);
    maelys_cli_sha256_final(&context, digest);
    to_hex(digest, out);
}

int maelys_cli_sha256_file(
    const char *path, size_t maximum_size,
    char out[MAELYS_CLI_SHA256_HEX_SIZE]) {
    unsigned char *bytes = NULL;
    size_t size = 0u;
    if (maelys_cli_read_regular_file(path, 0u, maximum_size, &bytes, &size) != 0)
        return -1;
    maelys_cli_sha256_hex(bytes, size, out);
    free(bytes);
    return 0;
}
