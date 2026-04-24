#ifndef IDMC_H
#define IDMC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void generate_nonce(uint8_t *nonce, size_t length);

void generate_dynamic_key(
    uint8_t *key,
    size_t key_len,
    const uint8_t *vertices,
    size_t v_len,
    const uint8_t *nonce,
    size_t nonce_len
);

void idmc_expand_key(
    const uint8_t *seed,
    size_t seed_len,
    uint8_t *keystream,
    size_t len
);

void idmc_encrypt(
    const uint8_t *plaintext,
    uint8_t *ciphertext,
    size_t length,
    const uint8_t *key,
    size_t key_len,
    const uint8_t *nonce,
    size_t nonce_len
);

void idmc_decrypt(
    const uint8_t *ciphertext,
    uint8_t *plaintext,
    size_t length,
    const uint8_t *key,
    size_t key_len,
    const uint8_t *nonce,
    size_t nonce_len
);

#ifdef __cplusplus
}
#endif

#endif
