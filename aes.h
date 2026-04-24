#ifndef AES_H
#define AES_H

#include <stddef.h>
#include <stdint.h>

void* init_aes_context(const uint8_t *key, size_t key_len, const uint8_t *iv, size_t iv_len, int enc);
void aes_encrypt_update(void *ctx, const uint8_t *input, uint8_t *output, size_t len);
void aes_cleanup(void *ctx);

#endif
