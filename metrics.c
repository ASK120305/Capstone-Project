// metrics.c
#include "metrics.h"

#include <string.h>
#include <stdlib.h>

#include <gtk/gtk.h>

static uint64_t now_us(void) {
    // GLib monotonic microseconds is cross-platform and high-resolution.
    return (uint64_t)g_get_monotonic_time();
}

void metrics_timer_start(MetricsTimer *t) {
    if (!t) return;
    t->t0_us = now_us();
}

uint64_t metrics_timer_stop_us(const MetricsTimer *t) {
    if (!t) return 0;
    const uint64_t t1 = now_us();
    return (t1 >= t->t0_us) ? (t1 - t->t0_us) : 0;
}

double metrics_us_to_ms(uint64_t us) {
    return (double)us / 1000.0;
}

double metrics_throughput_mb_s(size_t bytes, uint64_t elapsed_us) {
    if (elapsed_us == 0) return 0.0;
    const double sec = (double)elapsed_us / 1000000.0;
    const double mb = (double)bytes / (1024.0 * 1024.0);
    return mb / sec;
}

size_t metrics_estimate_memory_bytes(size_t data_bytes, size_t key_bytes, size_t nonce_bytes) {
    // Approx: input + output + scratch + key + nonce
    // Scratch approximates one extra buffer equal to data size.
    return (data_bytes * 3U) + key_bytes + nonce_bytes;
}

void metrics_run_eval(
    const char *name,
    void* (*init_fn)(const uint8_t*, size_t, const uint8_t*, size_t, int),
    void (*encrypt_fn)(void*, const uint8_t*, uint8_t*, size_t),
    void (*decrypt_fn)(void*, const uint8_t*, uint8_t*, size_t),
    void (*cleanup_fn)(void*),
    const uint8_t *in,
    size_t len,
    const uint8_t *key,
    size_t key_len,
    const uint8_t *nonce,
    size_t nonce_len,
    EvalMetrics *out
) {
    (void)name;
    if (!out) return;
    memset(out, 0, sizeof(*out));

    if (!init_fn || !encrypt_fn || !decrypt_fn || !cleanup_fn || !in || len == 0 || !key || !nonce || key_len == 0 || nonce_len == 0) {
        out->decrypt_ok = 0;
        return;
    }

    uint8_t *cipher = (uint8_t *)malloc(len + 32);
    uint8_t *plain = (uint8_t *)malloc(len + 32);
    if (!cipher || !plain) {
        free(cipher);
        free(plain);
        out->decrypt_ok = 0;
        return;
    }

    void *enc_ctx = init_fn(key, key_len, nonce, nonce_len, 1);
    void *dec_ctx = init_fn(key, key_len, nonce, nonce_len, 0);
    if (!enc_ctx || !dec_ctx) {
        if(enc_ctx) cleanup_fn(enc_ctx);
        if(dec_ctx) cleanup_fn(dec_ctx);
        out->decrypt_ok = 0;
        free(cipher); free(plain);
        return;
    }

    MetricsTimer t;
    metrics_timer_start(&t);
    encrypt_fn(enc_ctx, in, cipher, len);
    const uint64_t enc_us = metrics_timer_stop_us(&t);

    metrics_timer_start(&t);
    decrypt_fn(dec_ctx, cipher, plain, len);
    const uint64_t dec_us = metrics_timer_stop_us(&t);

    cleanup_fn(enc_ctx);
    cleanup_fn(dec_ctx);

    out->encrypt_ms = metrics_us_to_ms(enc_us);
    out->decrypt_ms = metrics_us_to_ms(dec_us);
    out->throughput_mb_s = metrics_throughput_mb_s(len, enc_us);
    out->memory_bytes = metrics_estimate_memory_bytes(len, key_len, nonce_len);
    out->decrypt_ok = (memcmp(in, plain, len) == 0) ? 1 : 0;

    free(cipher);
    free(plain);
}

