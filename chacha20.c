#include "chacha20.h"
#include <openssl/evp.h>

void* init_chacha20_context(const uint8_t *key, size_t key_len, const uint8_t *nonce, size_t nonce_len, int enc) {
    (void)key_len;
    (void)nonce_len;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return NULL;
    if (1 != EVP_CipherInit_ex(ctx, EVP_chacha20(), NULL, key, nonce, enc)) {
        EVP_CIPHER_CTX_free(ctx);
        return NULL;
    }
    return ctx;
}

void chacha20_process(void *ctx, const uint8_t *input, uint8_t *output, size_t len) {
    if (!ctx) return;
    int outl;
    EVP_CipherUpdate((EVP_CIPHER_CTX*)ctx, output, &outl, input, (int)len);
}

void chacha20_cleanup(void *ctx) {
    if (!ctx) return;
    int outl;
    unsigned char buf[16];
    EVP_CipherFinal_ex((EVP_CIPHER_CTX*)ctx, buf, &outl);
    EVP_CIPHER_CTX_free((EVP_CIPHER_CTX*)ctx);
}
