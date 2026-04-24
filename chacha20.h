#ifndef CHACHA20_H
#define CHACHA20_H

#include <stddef.h>
#include <stdint.h>

void* init_chacha20_context(const uint8_t *key, size_t key_len, const uint8_t *nonce, size_t nonce_len, int enc);
void chacha20_process(void *ctx, const uint8_t *input, uint8_t *output, size_t len);
void chacha20_cleanup(void *ctx);

#endif
