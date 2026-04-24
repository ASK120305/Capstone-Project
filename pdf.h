#ifndef PDF_H
#define PDF_H

#include "metrics.h"

#include <stddef.h>

typedef struct {
    EvalMetrics *results;
    const char *const *names;
    size_t count;
    const char *source_file;
    int success;
} MetricsData;

void generate_pdf_report(
    const char *input_filename,
    MetricsData *metrics
);

#endif
