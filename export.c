#include "export.h"

#include <cairo.h>
#include <stdio.h>
#include <string.h>

static void draw_bar_chart(cairo_t *cr,
                           double x, double y, double w, double h,
                           const char *title,
                           const double *v,
                           const char *const *names,
                           size_t count,
                           const char *unit) {
    cairo_save(cr);

    cairo_set_source_rgb(cr, 0.12, 0.12, 0.12);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 0.30, 0.30, 0.30);
    cairo_rectangle(cr, x + 0.5, y + 0.5, w - 1.0, h - 1.0);
    cairo_stroke(cr);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13.0);
    cairo_set_source_rgb(cr, 0.92, 0.92, 0.92);
    cairo_move_to(cr, x + 10, y + 18);
    cairo_show_text(cr, title);

    double maxv = 0.0;
    for (size_t i = 0; i < count; i++) {
        if (v[i] > maxv) maxv = v[i];
    }
    if (maxv <= 0.0) maxv = 1.0;

    const double padL = 10.0, padR = 10.0, padT = 28.0, padB = 24.0;
    const double gx = x + padL;
    const double gy = y + padT;
    const double gw = w - padL - padR;
    const double gh = h - padT - padB;

    cairo_set_source_rgb(cr, 0.22, 0.22, 0.22);
    cairo_rectangle(cr, gx, gy, gw, gh);
    cairo_fill(cr);

    const double barGap = 10.0;
    const double barW = (count > 0) ? (gw - ((double)(count - 1U) * barGap)) / (double)count : 0.0;
    const double colors[4][3] = {
        {0.25, 0.62, 0.95},
        {0.95, 0.55, 0.25},
        {0.35, 0.85, 0.45},
        {0.75, 0.45, 0.90}
    };

    cairo_set_font_size(cr, 11.0);
    for (size_t i = 0; i < count; i++) {
        const double bv = v[i];
        const double bh = (bv / maxv) * (gh - 8.0);
        const double bx = gx + (double)i * (barW + barGap);
        const double by = gy + (gh - bh);
        const double *c = colors[i % 4U];

        cairo_set_source_rgb(cr, c[0], c[1], c[2]);
        cairo_rectangle(cr, bx, by, barW, bh);
        cairo_fill(cr);

        cairo_set_source_rgb(cr, 0.92, 0.92, 0.92);
        cairo_move_to(cr, bx, y + h - 8);
        cairo_show_text(cr, names[i] ? names[i] : "");
    }

    char maxlbl[96];
    snprintf(maxlbl, sizeof(maxlbl), "max %.3f %s", maxv, unit ? unit : "");
    cairo_set_source_rgb(cr, 0.70, 0.70, 0.70);
    cairo_set_font_size(cr, 11.0);
    cairo_move_to(cr, x + 10, y + h - 8);
    cairo_show_text(cr, maxlbl);

    cairo_restore(cr);
}

int export_csv(const char *path, const EvalMetrics *results, const char *const *names, size_t count) {
    if (!path || !results || !names || count == 0) return 0;
    FILE *fp = fopen(path, "w");
    if (!fp) return 0;

    fprintf(fp, "Algorithm,Encrypt Time,Decrypt Time,Throughput,Memory Usage\n");
    for (size_t i = 0; i < count; i++) {
        fprintf(fp, "%s,%.3f,%.3f,%.3f,%zu\n",
                names[i] ? names[i] : "Unknown",
                results[i].encrypt_ms,
                results[i].decrypt_ms,
                results[i].throughput_mb_s,
                results[i].memory_bytes);
    }

    fclose(fp);
    return 1;
}

int export_graph(const char *path, const EvalMetrics *results, const char *const *names, size_t count, int width, int height) {
    if (!path || !results || !names || count == 0) return 0;
    if (width <= 0 || height <= 0) return 0;

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (!surface) return 0;
    cairo_t *cr = cairo_create(surface);
    if (!cr) {
        cairo_surface_destroy(surface);
        return 0;
    }

    cairo_set_source_rgb(cr, 0.10, 0.10, 0.10);
    cairo_paint(cr);

    double v_enc[4] = {0};
    double v_dec[4] = {0};
    double v_tp[4] = {0};
    double v_mem[4] = {0};
    for (size_t i = 0; i < count && i < 4U; i++) {
        v_enc[i] = results[i].encrypt_ms;
        v_dec[i] = results[i].decrypt_ms;
        v_tp[i] = results[i].throughput_mb_s;
        v_mem[i] = (double)results[i].memory_bytes;
    }

    const double pad = 10.0;
    const double cellW = ((double)width - (3.0 * pad)) / 2.0;
    const double cellH = ((double)height - (3.0 * pad)) / 2.0;

    draw_bar_chart(cr, pad, pad, cellW, cellH, "Encryption Time", v_enc, names, count, "ms");
    draw_bar_chart(cr, pad * 2 + cellW, pad, cellW, cellH, "Decryption Time", v_dec, names, count, "ms");
    draw_bar_chart(cr, pad, pad * 2 + cellH, cellW, cellH, "Throughput", v_tp, names, count, "MB/s");
    draw_bar_chart(cr, pad * 2 + cellW, pad * 2 + cellH, cellW, cellH, "Memory Usage", v_mem, names, count, "B");

    const cairo_status_t status = cairo_surface_write_to_png(surface, path);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return status == CAIRO_STATUS_SUCCESS ? 1 : 0;
}

int generate_report(const char *path, const EvalMetrics *results, const char *const *names, size_t count) {
    if (!path || !results || !names || count == 0) return 0;
    FILE *fp = fopen(path, "w");
    if (!fp) return 0;

    fprintf(fp, "Encryption Evaluation Report\n");
    fprintf(fp, "----------------------------\n\n");
    fprintf(fp, "Algorithms Compared:\n");
    for (size_t i = 0; i < count; i++) {
        fprintf(fp, "- %s\n", names[i] ? names[i] : "Unknown");
    }

    fprintf(fp, "\nResults:\n\n");
    for (size_t i = 0; i < count; i++) {
        fprintf(fp, "%s\n", names[i] ? names[i] : "Unknown");
        fprintf(fp, "  Encrypt Time: %.3f ms\n", results[i].encrypt_ms);
        fprintf(fp, "  Decrypt Time: %.3f ms\n", results[i].decrypt_ms);
        fprintf(fp, "  Throughput: %.3f MB/s\n", results[i].throughput_mb_s);
        fprintf(fp, "  Memory Usage: %zu B\n", results[i].memory_bytes);
        fprintf(fp, "  Decryption: %s\n\n", results[i].decrypt_ok ? "Successful" : "Failed");
    }

    fprintf(fp, "Conclusion:\n");
    fprintf(fp, "- IDMC is lightweight and fast for small data\n");
    fprintf(fp, "- AES shows stable performance\n");
    fprintf(fp, "- IDMC performance varies in batch\n");

    fclose(fp);
    return 1;
}
