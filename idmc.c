#include "idmc.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define IDMC_FORCE_INLINE static inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define IDMC_FORCE_INLINE __forceinline
#else
#define IDMC_FORCE_INLINE static inline
#endif

void generate_nonce(uint8_t *nonce, size_t length) {
    if (!nonce || length == 0) return;
    for (size_t i = 0; i < length; i++) {
        nonce[i] = (uint8_t)(i & 0xFF);
    }
}

void generate_dynamic_key(
    uint8_t *key,
    size_t key_len,
    const uint8_t *vertices,
    size_t v_len,
    const uint8_t *nonce,
    size_t nonce_len
) {
    if (!key || key_len == 0) return;
    
    size_t v_idx = 0;
    size_t n_idx = 0;
    
    for (size_t i = 0; i < key_len; i++) {
        uint8_t v = (vertices && v_len > 0) ? vertices[v_idx] : 0xAA;
        uint8_t n = (nonce && nonce_len > 0) ? nonce[n_idx] : 0x55;
        
        key[i] = v ^ n;
        
        if (vertices && v_len > 0) {
            v_idx++;
            if (v_idx == v_len) v_idx = 0;
        }
        if (nonce && nonce_len > 0) {
            n_idx++;
            if (n_idx == nonce_len) n_idx = 0;
        }
    }
}

IDMC_FORCE_INLINE void idmc_process(
    const uint8_t *in,
    uint8_t *out,
    size_t length,
    const uint8_t *key,
    size_t key_len
) {
    if (!in || !out || !key || length == 0 || key_len == 0) return;

    size_t key_blocks = key_len / 8;
    if (key_blocks == 0) {
        size_t k_idx = 0;
        for (size_t i = 0; i < length; i++) {
            out[i] = in[i] ^ key[k_idx++];
            if (k_idx == key_len) k_idx = 0;
        }
        return;
    }

    const uint64_t *k_start = (const uint64_t *)key;
    const uint64_t *k_end = k_start + key_blocks;
    const uint64_t *k_ptr = k_start;

    const uint8_t *d_in_8 = in;
    uint8_t *d_out_8 = out;
    size_t len_rem = length;

#if defined(__AVX2__)
    size_t chunks32 = len_rem / 32;
    if (chunks32 > 0) {
        size_t exp_len = key_len;
        while (exp_len % 32 != 0) exp_len += key_len;

#if defined(__GNUC__) || defined(__clang__)
        __attribute__((aligned(32))) uint8_t exp_key[1024];
#elif defined(_MSC_VER)
        __declspec(align(32)) uint8_t exp_key[1024];
#else
        uint8_t exp_key[1024];
#endif
        if (exp_len <= sizeof(exp_key)) {
            for (size_t i = 0; i < exp_len; i++) {
                exp_key[i] = key[i % key_len];
            }
            
            size_t exp_chunks = exp_len / 32;
            size_t k_idx = 0;
            const __m256i* k_simd = (const __m256i*)exp_key;

            for (size_t i = 0; i < chunks32; i++) {
                __m256i data;
                if (((uintptr_t)d_in_8 & 31) == 0) {
                    data = _mm256_load_si256((const __m256i *)d_in_8);
                } else {
                    data = _mm256_loadu_si256((const __m256i *)d_in_8);
                }

                __m256i k = _mm256_load_si256(&k_simd[k_idx]);
                k_idx++;
                if (k_idx == exp_chunks) k_idx = 0;

                __m256i res = _mm256_xor_si256(data, k);

                if (((uintptr_t)d_out_8 & 31) == 0) {
                    _mm256_store_si256((__m256i *)d_out_8, res);
                } else {
                    _mm256_storeu_si256((__m256i *)d_out_8, res);
                }

                d_in_8 += 32;
                d_out_8 += 32;
                len_rem -= 32;
            }
            
            size_t avx_processed = chunks32 * 32;
            k_ptr = k_start + ((avx_processed % (key_blocks * 8)) / 8);
        } else {
            const uint64_t *d_in64 = (const uint64_t *)d_in_8;
            uint64_t *d_out64 = (uint64_t *)d_out_8;
            for (size_t i = 0; i < chunks32; i++) {
                uint64_t k0 = *k_ptr++; if (k_ptr == k_end) k_ptr = k_start;
                uint64_t k1 = *k_ptr++; if (k_ptr == k_end) k_ptr = k_start;
                uint64_t k2 = *k_ptr++; if (k_ptr == k_end) k_ptr = k_start;
                uint64_t k3 = *k_ptr++; if (k_ptr == k_end) k_ptr = k_start;

                d_out64[0] = d_in64[0] ^ k0;
                d_out64[1] = d_in64[1] ^ k1;
                d_out64[2] = d_in64[2] ^ k2;
                d_out64[3] = d_in64[3] ^ k3;

                d_in64 += 4;
                d_out64 += 4;
                len_rem -= 32;
            }
            d_in_8 = (const uint8_t *)d_in64;
            d_out_8 = (uint8_t *)d_out64;
        }
    }
#else
    size_t chunks32 = len_rem / 32;
    if (chunks32 > 0) {
        const uint64_t *d_in64 = (const uint64_t *)d_in_8;
        uint64_t *d_out64 = (uint64_t *)d_out_8;
        for (size_t i = 0; i < chunks32; i++) {
            uint64_t k0 = *k_ptr++; if (k_ptr == k_end) k_ptr = k_start;
            uint64_t k1 = *k_ptr++; if (k_ptr == k_end) k_ptr = k_start;
            uint64_t k2 = *k_ptr++; if (k_ptr == k_end) k_ptr = k_start;
            uint64_t k3 = *k_ptr++; if (k_ptr == k_end) k_ptr = k_start;

            d_out64[0] = d_in64[0] ^ k0;
            d_out64[1] = d_in64[1] ^ k1;
            d_out64[2] = d_in64[2] ^ k2;
            d_out64[3] = d_in64[3] ^ k3;

            d_in64 += 4;
            d_out64 += 4;
            len_rem -= 32;
        }
        d_in_8 = (const uint8_t *)d_in64;
        d_out_8 = (uint8_t *)d_out64;
    }
#endif

    const uint64_t *d_in = (const uint64_t *)d_in_8;
    uint64_t *d_out = (uint64_t *)d_out_8;

    size_t chunks8 = len_rem / 8;
    for (size_t i = 0; i < chunks8; i++) {
        uint64_t k0 = *k_ptr++;
        if (k_ptr == k_end) k_ptr = k_start;
        
        *d_out = *d_in ^ k0;
        d_in++;
        d_out++;
    }

    size_t remain_bytes = len_rem % 8;
    if (remain_bytes > 0) {
        const uint8_t *b_in = (const uint8_t *)d_in;
        uint8_t *b_out = (uint8_t *)d_out;
        uint64_t k0 = *k_ptr;
        const uint8_t *kb = (const uint8_t *)&k0;
        
        for (size_t i = 0; i < remain_bytes; i++) {
            b_out[i] = b_in[i] ^ kb[i];
        }
    }
}

void idmc_encrypt(
    const uint8_t *plaintext,
    uint8_t *ciphertext,
    size_t length,
    const uint8_t *key,
    size_t key_len,
    const uint8_t *nonce,
    size_t nonce_len
) {
    (void)nonce;
    (void)nonce_len;
    idmc_process(plaintext, ciphertext, length, key, key_len);
}

void idmc_decrypt(
    const uint8_t *ciphertext,
    uint8_t *plaintext,
    size_t length,
    const uint8_t *key,
    size_t key_len,
    const uint8_t *nonce,
    size_t nonce_len
) {
    (void)nonce;
    (void)nonce_len;
    idmc_process(ciphertext, plaintext, length, key, key_len);
}
