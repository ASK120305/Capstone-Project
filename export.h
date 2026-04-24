#ifndef EXPORT_H
#define EXPORT_H

#include "metrics.h"

#include <stddef.h>

int export_csv(const char *path, const EvalMetrics *results, const char *const *names, size_t count);
int export_graph(const char *path, const EvalMetrics *results, const char *const *names, size_t count, int width, int height);
int generate_report(const char *path, const EvalMetrics *results, const char *const *names, size_t count);

#endif
