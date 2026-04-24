#include "aes.h"
#include <openssl/evp.h>

void* init_aes_context(const uint8_t *key, size_t key_len, const uint8_t *iv, size_t iv_len, int enc) {
    (void)key_len;
    (void)iv_len;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return NULL;
    if (1 != EVP_CipherInit_ex(ctx, EVP_aes_128_ctr(), NULL, key, iv, enc)) {
        EVP_CIPHER_CTX_free(ctx);
        return NULL;
    }
    return ctx;
}

void aes_encrypt_update(void *ctx, const uint8_t *input, uint8_t *output, size_t len) {
    if (!ctx) return;
    int outl;
    EVP_CipherUpdate((EVP_CIPHER_CTX*)ctx, output, &outl, input, (int)len);
}

void aes_cleanup(void *ctx) {
    if (!ctx) return;
    int outl;
    unsigned char buf[16];
    EVP_CipherFinal_ex((EVP_CIPHER_CTX*)ctx, buf, &outl);
    EVP_CIPHER_CTX_free((EVP_CIPHER_CTX*)ctx);
}
