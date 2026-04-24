#ifndef PRESENT_H
#define PRESENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void *present_init(const uint8_t *key, size_t key_len, const uint8_t *nonce, size_t nonce_len, int enc);
void present_process(void *ctx, const uint8_t *input, uint8_t *output, size_t len);
void present_cleanup(void *ctx);

#ifdef __cplusplus
}
#endif

#endif
