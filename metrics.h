// metrics.h
#ifndef METRICS_H
#define METRICS_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    double encrypt_ms;
    double decrypt_ms;
    double throughput_mb_s;
    size_t memory_bytes;
    int decrypt_ok;
} EvalMetrics;

typedef struct {
    uint64_t t0_us;
} MetricsTimer;

void metrics_timer_start(MetricsTimer *t);
uint64_t metrics_timer_stop_us(const MetricsTimer *t);
double metrics_us_to_ms(uint64_t us);
double metrics_throughput_mb_s(size_t bytes, uint64_t elapsed_us);
size_t metrics_estimate_memory_bytes(size_t data_bytes, size_t key_bytes, size_t nonce_bytes);

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
);

#endif
