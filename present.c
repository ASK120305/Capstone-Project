#include "present.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t round_keys[32];
    uint64_t counter;
} present_ctx_t;

static const uint8_t PRESENT_SBOX4[16] = {
    0xC, 0x5, 0x6, 0xB,
    0x9, 0x0, 0xA, 0xD,
    0x3, 0xE, 0xF, 0x8,
    0x4, 0x7, 0x1, 0x2
};

static uint8_t PRESENT_SBOX8[256];
static uint64_t PRESENT_PLAYER[8][256];
static int PRESENT_TABLES_READY = 0;

static inline uint64_t present_load_be64(const uint8_t *p) {
    return ((uint64_t)p[0] << 56) |
           ((uint64_t)p[1] << 48) |
           ((uint64_t)p[2] << 40) |
           ((uint64_t)p[3] << 32) |
           ((uint64_t)p[4] << 24) |
           ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] << 8) |
           (uint64_t)p[7];
}

static inline void present_store_be64(uint64_t v, uint8_t *p) {
    p[0] = (uint8_t)(v >> 56);
    p[1] = (uint8_t)(v >> 48);
    p[2] = (uint8_t)(v >> 40);
    p[3] = (uint8_t)(v >> 32);
    p[4] = (uint8_t)(v >> 24);
    p[5] = (uint8_t)(v >> 16);
    p[6] = (uint8_t)(v >> 8);
    p[7] = (uint8_t)v;
}

static inline uint64_t present_sbox_layer(uint64_t x) {
    return (uint64_t)PRESENT_SBOX8[(x >> 56) & 0xFFu] << 56 |
           (uint64_t)PRESENT_SBOX8[(x >> 48) & 0xFFu] << 48 |
           (uint64_t)PRESENT_SBOX8[(x >> 40) & 0xFFu] << 40 |
           (uint64_t)PRESENT_SBOX8[(x >> 32) & 0xFFu] << 32 |
           (uint64_t)PRESENT_SBOX8[(x >> 24) & 0xFFu] << 24 |
           (uint64_t)PRESENT_SBOX8[(x >> 16) & 0xFFu] << 16 |
           (uint64_t)PRESENT_SBOX8[(x >> 8) & 0xFFu] << 8 |
           (uint64_t)PRESENT_SBOX8[x & 0xFFu];
}

static inline uint64_t present_player(uint64_t x) {
    return PRESENT_PLAYER[0][(x >> 56) & 0xFFu] |
           PRESENT_PLAYER[1][(x >> 48) & 0xFFu] |
           PRESENT_PLAYER[2][(x >> 40) & 0xFFu] |
           PRESENT_PLAYER[3][(x >> 32) & 0xFFu] |
           PRESENT_PLAYER[4][(x >> 24) & 0xFFu] |
           PRESENT_PLAYER[5][(x >> 16) & 0xFFu] |
           PRESENT_PLAYER[6][(x >> 8) & 0xFFu] |
           PRESENT_PLAYER[7][x & 0xFFu];
}

#define PRESENT_ROUND(S, RK, I) do { \
    (S) ^= (RK)[(I)];               \
    (S) = present_sbox_layer(S);    \
    (S) = present_player(S);        \
} while (0)

static inline uint64_t present_encrypt_u64(const uint64_t *rk, uint64_t state) {
    PRESENT_ROUND(state, rk, 0);
    PRESENT_ROUND(state, rk, 1);
    PRESENT_ROUND(state, rk, 2);
    PRESENT_ROUND(state, rk, 3);
    PRESENT_ROUND(state, rk, 4);
    PRESENT_ROUND(state, rk, 5);
    PRESENT_ROUND(state, rk, 6);
    PRESENT_ROUND(state, rk, 7);
    PRESENT_ROUND(state, rk, 8);
    PRESENT_ROUND(state, rk, 9);
    PRESENT_ROUND(state, rk, 10);
    PRESENT_ROUND(state, rk, 11);
    PRESENT_ROUND(state, rk, 12);
    PRESENT_ROUND(state, rk, 13);
    PRESENT_ROUND(state, rk, 14);
    PRESENT_ROUND(state, rk, 15);
    PRESENT_ROUND(state, rk, 16);
    PRESENT_ROUND(state, rk, 17);
    PRESENT_ROUND(state, rk, 18);
    PRESENT_ROUND(state, rk, 19);
    PRESENT_ROUND(state, rk, 20);
    PRESENT_ROUND(state, rk, 21);
    PRESENT_ROUND(state, rk, 22);
    PRESENT_ROUND(state, rk, 23);
    PRESENT_ROUND(state, rk, 24);
    PRESENT_ROUND(state, rk, 25);
    PRESENT_ROUND(state, rk, 26);
    PRESENT_ROUND(state, rk, 27);
    PRESENT_ROUND(state, rk, 28);
    PRESENT_ROUND(state, rk, 29);
    PRESENT_ROUND(state, rk, 30);
    state ^= rk[31];
    return state;
}

static void present_init_tables(void) {
    if (PRESENT_TABLES_READY) {
        return;
    }

    for (unsigned v = 0; v < 256; ++v) {
        PRESENT_SBOX8[v] = (uint8_t)((PRESENT_SBOX4[v >> 4] << 4) | PRESENT_SBOX4[v & 0x0Fu]);
    }

    for (unsigned b = 0; b < 8; ++b) {
        const unsigned src_byte = 7u - b;
        for (unsigned v = 0; v < 256; ++v) {
            uint64_t out = 0;
            if (v & 0x80u) out |= 1ull << (((src_byte * 8u + 0u) == 63u) ? 63u : ((src_byte * 8u + 0u) * 16u) % 63u);
            if (v & 0x40u) out |= 1ull << (((src_byte * 8u + 1u) == 63u) ? 63u : ((src_byte * 8u + 1u) * 16u) % 63u);
            if (v & 0x20u) out |= 1ull << (((src_byte * 8u + 2u) == 63u) ? 63u : ((src_byte * 8u + 2u) * 16u) % 63u);
            if (v & 0x10u) out |= 1ull << (((src_byte * 8u + 3u) == 63u) ? 63u : ((src_byte * 8u + 3u) * 16u) % 63u);
            if (v & 0x08u) out |= 1ull << (((src_byte * 8u + 4u) == 63u) ? 63u : ((src_byte * 8u + 4u) * 16u) % 63u);
            if (v & 0x04u) out |= 1ull << (((src_byte * 8u + 5u) == 63u) ? 63u : ((src_byte * 8u + 5u) * 16u) % 63u);
            if (v & 0x02u) out |= 1ull << (((src_byte * 8u + 6u) == 63u) ? 63u : ((src_byte * 8u + 6u) * 16u) % 63u);
            if (v & 0x01u) out |= 1ull << (((src_byte * 8u + 7u) == 63u) ? 63u : ((src_byte * 8u + 7u) * 16u) % 63u);
            PRESENT_PLAYER[b][v] = out;
        }
    }

    PRESENT_TABLES_READY = 1;
}

void *present_init(const uint8_t *key, size_t key_len, const uint8_t *nonce, size_t nonce_len, int enc) {
    (void)enc;

    present_init_tables();

    present_ctx_t *ctx = (present_ctx_t *)malloc(sizeof(present_ctx_t));
    if (!ctx) {
        return NULL;
    }

    uint8_t k[10] = {0};
    if (key && key_len) {
        const size_t n = (key_len < 10u) ? key_len : 10u;
        memcpy(k, key, n);
    }

    uint64_t high = ((uint64_t)k[0] << 56) |
                    ((uint64_t)k[1] << 48) |
                    ((uint64_t)k[2] << 40) |
                    ((uint64_t)k[3] << 32) |
                    ((uint64_t)k[4] << 24) |
                    ((uint64_t)k[5] << 16) |
                    ((uint64_t)k[6] << 8) |
                    (uint64_t)k[7];
    uint16_t low = (uint16_t)(((uint16_t)k[8] << 8) | k[9]);

    ctx->round_keys[0] = high;
    for (uint32_t round = 1; round < 32; ++round) {
        const uint64_t old_high = high;
        high = (old_high << 61) | ((uint64_t)low << 45) | (old_high >> 19);
        low = (uint16_t)(old_high >> 3);

        high = (high & 0x0FFFFFFFFFFFFFFFull) | ((uint64_t)PRESENT_SBOX4[high >> 60] << 60);

        high ^= (uint64_t)(round >> 1);
        low ^= (uint16_t)((round & 1u) << 15);

        ctx->round_keys[round] = high;
    }

    uint8_t ctr[8] = {0};
    if (nonce && nonce_len) {
        const size_t n = (nonce_len < 8u) ? nonce_len : 8u;
        memcpy(ctr, nonce, n);
    }
    ctx->counter = present_load_be64(ctr);

    return ctx;
}

void present_process(void *ctx, const uint8_t *input, uint8_t *output, size_t len) {
    if (!ctx || !input || !output || !len) {
        return;
    }

    present_ctx_t *c = (present_ctx_t *)ctx;
    const uint64_t *rk = c->round_keys;
    uint64_t ctr = c->counter;

    while (len >= 16u) {
        const uint64_t ks0 = present_encrypt_u64(rk, ctr);
        const uint64_t ks1 = present_encrypt_u64(rk, ctr + 1u);

        const uint64_t in0 = present_load_be64(input);
        const uint64_t in1 = present_load_be64(input + 8);

        present_store_be64(in0 ^ ks0, output);
        present_store_be64(in1 ^ ks1, output + 8);

        ctr += 2u;
        input += 16;
        output += 16;
        len -= 16u;
    }

    if (len >= 8u) {
        const uint64_t ks = present_encrypt_u64(rk, ctr++);
        const uint64_t in = present_load_be64(input);
        present_store_be64(in ^ ks, output);
        input += 8;
        output += 8;
        len -= 8u;
    }

    if (len) {
        const uint64_t ks = present_encrypt_u64(rk, ctr++);
        uint8_t stream[8];
        present_store_be64(ks, stream);

        output[0] = input[0] ^ stream[0];
        if (len > 1u) output[1] = input[1] ^ stream[1];
        if (len > 2u) output[2] = input[2] ^ stream[2];
        if (len > 3u) output[3] = input[3] ^ stream[3];
        if (len > 4u) output[4] = input[4] ^ stream[4];
        if (len > 5u) output[5] = input[5] ^ stream[5];
        if (len > 6u) output[6] = input[6] ^ stream[6];
    }

    c->counter = ctr;
}

void present_cleanup(void *ctx) {
    free(ctx);
}
