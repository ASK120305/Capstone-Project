#include "pdf.h"

#include <cairo-pdf.h>
#include <cairo.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const double PAGE_W = 595.0;
static const double PAGE_H = 842.0;

static const char *basename_ptr(const char *path) {
    const char *base = path;
    const char *p = path;
    while (p && *p) {
        if (*p == '/' || *p == '\\') base = p + 1;
        p++;
    }
    return base;
}

static void make_report_filename(const char *input_filename, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';

    if (!input_filename || !input_filename[0]) {
        snprintf(out, out_size, "report.pdf");
        return;
    }

    const char *base = basename_ptr(input_filename);
    char stem[256];
    strncpy(stem, base, sizeof(stem) - 1U);
    stem[sizeof(stem) - 1U] = '\0';
    char *dot = strrchr(stem, '.');
    if (dot) *dot = '\0';
    if (stem[0] == '\0') snprintf(stem, sizeof(stem), "report");
    snprintf(out, out_size, "%s_report.pdf", stem);
}

// PDF PAGE 1: COVER & SUMMARY TABLE
static void draw_summary_page(cairo_t *cr, const char *source_file, const MetricsData *metrics) {
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    // Title
    cairo_set_source_rgb(cr, 0.1, 0.2, 0.4);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 24.0);
    cairo_move_to(cr, 40.0, 60.0);
    cairo_show_text(cr, "Encryption Evaluation Report");

    cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
    cairo_set_font_size(cr, 12.0);
    cairo_move_to(cr, 40.0, 80.0);
    cairo_show_text(cr, "Dataset Name: ");
    cairo_show_text(cr, source_file ? source_file : "Unknown");

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char datebuf[64];
    if (tm_info) {
        strftime(datebuf, sizeof(datebuf), "%Y-%m-%d %H:%M:%S", tm_info);
    } else {
        snprintf(datebuf, sizeof(datebuf), "Unknown");
    }
    cairo_move_to(cr, 40.0, 100.0);
    cairo_show_text(cr, "Timestamp: ");
    cairo_show_text(cr, datebuf);

    // Summary Table
    cairo_set_source_rgb(cr, 0.1, 0.2, 0.4);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 16.0);
    cairo_move_to(cr, 40.0, 160.0);
    cairo_show_text(cr, "Summary Table");

    const double x = 40.0;
    const double y = 180.0;
    const double row_h = 30.0;
    const double col_w[5] = {140.0, 90.0, 90.0, 110.0, 80.0};
    const char *headers[5] = { "Algorithm", "Encrypt (ms)", "Decrypt (ms)", "Throughput", "Memory" };

    // Draw header row background
    cairo_set_source_rgb(cr, 0.9, 0.9, 0.95);
    cairo_rectangle(cr, x, y, 510.0, row_h);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 11.0);
    double cursor_x = x;
    for (int c = 0; c < 5; c++) {
        cairo_move_to(cr, cursor_x + 5.0, y + 20.0);
        cairo_show_text(cr, headers[c]);
        cursor_x += col_w[c];
    }

    cairo_set_line_width(cr, 0.5);
    cairo_move_to(cr, x, y + row_h);
    cairo_line_to(cr, x + 510.0, y + row_h);
    cairo_stroke(cr);

    cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10.0);

    for (size_t i = 0; i < metrics->count; i++) {
        const double ry = y + row_h + ((double)i * row_h);
        char enc[64], dec[64], tp[64], mem[64];
        snprintf(enc, sizeof(enc), "%.3f", metrics->results[i].encrypt_ms);
        snprintf(dec, sizeof(dec), "%.3f", metrics->results[i].decrypt_ms);
        snprintf(tp, sizeof(tp), "%.3f MB/s", metrics->results[i].throughput_mb_s);
        snprintf(mem, sizeof(mem), "%zu B", metrics->results[i].memory_bytes);

        // Substring algorithm name if too long
        char algo[64];
        strncpy(algo, metrics->names[i] ? metrics->names[i] : "Unknown", sizeof(algo) - 1);
        algo[sizeof(algo)-1] = '\0';
        char *paren = strchr(algo, '(');
        if (paren) {
            *paren = '\0';
            for (int k = strlen(algo) - 1; k >= 0 && algo[k] == ' '; k--) algo[k] = '\0';
        }

        const char *vals[5] = { algo, enc, dec, tp, mem };

        if (i % 2 == 1) {
            cairo_set_source_rgb(cr, 0.97, 0.97, 0.97);
            cairo_rectangle(cr, x, ry, 510.0, row_h);
            cairo_fill(cr);
        }

        cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
        double cx = x;
        for (int c = 0; c < 5; c++) {
            cairo_move_to(cr, cx + 5.0, ry + 20.0);
            cairo_show_text(cr, vals[c]);
            cx += col_w[c];
        }

        cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
        cairo_move_to(cr, x, ry + row_h);
        cairo_line_to(cr, x + 510.0, ry + row_h);
        cairo_stroke(cr);
    }
}

// CAIRO PDF BAR CHART
static void draw_pdf_bar_chart(cairo_t *cr,
                               double x, double y, double w, double h,
                               const char *title,
                               const double v[4],
                               const char *names_in[4],
                               const char *unit) {
    cairo_save(cr);

    // Light background for graph frame
    cairo_set_source_rgb(cr, 0.98, 0.98, 0.98);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
    cairo_rectangle(cr, x + 0.5, y + 0.5, w - 1.0, h - 1.0);
    cairo_stroke(cr);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 14.0);
    cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
    cairo_move_to(cr, x + 15, y + 25);
    cairo_show_text(cr, title);

    double sorted_v[4];
    for (int i = 0; i < 4; i++) sorted_v[i] = v[i];
    for (int i = 0; i < 3; i++) {
        for (int j = i + 1; j < 4; j++) {
            if (sorted_v[i] < sorted_v[j]) {
                double tmp = sorted_v[i];
                sorted_v[i] = sorted_v[j];
                sorted_v[j] = tmp;
            }
        }
    }
    double maxv_true = sorted_v[0];
    double second_highest = sorted_v[1];
    if (second_highest <= 0.0) second_highest = 1.0;

    double maxv_drawn = maxv_true;
    int is_capped = 0;
    if (maxv_true > second_highest * 5.0 && second_highest > 0.0) {
        maxv_drawn = second_highest * 2.0;
        is_capped = 1;
    }
    if (maxv_drawn <= 0.0) maxv_drawn = 1.0;

    const double padL = 55.0; 
    const double padR = 15.0;
    const double padT = 45.0; 
    const double padB = 45.0; 
    const double gx = x + padL;
    const double gy = y + padT;
    const double gw = w - padL - padR;
    const double gh = h - padT - padB;

    if (gw < 10 || gh < 10) { 
        cairo_restore(cr); 
        return; 
    }

    cairo_set_source_rgb(cr, 0.95, 0.95, 0.95);
    cairo_rectangle(cr, gx, gy, gw, gh);
    cairo_fill(cr);

    // Y Label
    cairo_save(cr);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10.0);
    cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
    cairo_translate(cr, x + 12, gy + gh / 2.0);
    cairo_rotate(cr, -1.57079632679);
    cairo_text_extents_t te;
    char ylabel[64];
    snprintf(ylabel, sizeof(ylabel), "Value (%s)", unit);
    cairo_text_extents(cr, ylabel, &te);
    cairo_move_to(cr, -te.width / 2.0, 0);
    cairo_show_text(cr, ylabel);
    cairo_restore(cr);

    // Grid
    cairo_set_line_width(cr, 0.5);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 9.0);
    for (int i = 0; i <= 4; i++) {
        double line_y = gy + gh - (i * gh / 4.0);
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.1);
        cairo_move_to(cr, gx, line_y);
        cairo_line_to(cr, gx + gw, line_y);
        cairo_stroke(cr);

        double tick_val = (i / 4.0) * maxv_drawn;
        char yt[32];
        if (tick_val >= 100.0) snprintf(yt, sizeof(yt), "%.0f", tick_val);
        else if (tick_val >= 10.0) snprintf(yt, sizeof(yt), "%.1f", tick_val);
        else snprintf(yt, sizeof(yt), "%.2f", tick_val);
        if (i == 4 && is_capped) snprintf(yt, sizeof(yt), "> %s", yt);

        cairo_text_extents(cr, yt, &te);
        cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
        cairo_move_to(cr, gx - te.width - 4, line_y + 3);
        cairo_show_text(cr, yt);
    }

    const double barGap = gw * 0.12; 
    const double barW = (gw - barGap * 5) / 4.0;

    const double colors[4][3] = {
        {0.20, 0.60, 1.00}, 
        {1.00, 0.55, 0.15}, 
        {0.20, 0.85, 0.45}, 
        {0.65, 0.35, 0.90}  
    };

    for (int i = 0; i < 4; i++) {
        double bv = v[i];
        int capped_this = 0;
        if (bv > maxv_drawn && is_capped) {
            bv = maxv_drawn;
            capped_this = 1;
        }

        double bh = (bv / maxv_drawn) * gh;
        if (bh < 2.0 && v[i] > 0.0) bh = 2.0; 
        double bx = gx + barGap + i * (barW + barGap);
        double by = gy + (gh - bh);

        cairo_set_source_rgb(cr, colors[i][0], colors[i][1], colors[i][2]);
        cairo_rectangle(cr, bx, by, barW, bh);
        cairo_fill(cr);

        if (capped_this) {
            cairo_set_source_rgb(cr, 0.95, 0.95, 0.95);
            cairo_move_to(cr, bx - 1, by + 4);
            cairo_line_to(cr, bx + barW/2, by - 2);
            cairo_line_to(cr, bx + barW + 1, by + 4);
            cairo_line_to(cr, bx + barW + 1, by - 2);
            cairo_line_to(cr, bx - 1, by - 2);
            cairo_fill(cr);
        }

        char val_label[64];
        if (v[i] >= 100.0) snprintf(val_label, sizeof(val_label), "%.0f %s", v[i], unit);
        else if (v[i] >= 10.0) snprintf(val_label, sizeof(val_label), "%.1f %s", v[i], unit);
        else snprintf(val_label, sizeof(val_label), "%.2f %s", v[i], unit);

        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 10.0);
        cairo_text_extents(cr, val_label, &te);

        double tx = bx + (barW - te.width) / 2.0;
        double ty = by - 5.0;
        cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
        cairo_move_to(cr, tx, ty);
        cairo_show_text(cr, val_label);

        cairo_save(cr);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 11.0);
        
        char short_name[64];
        strncpy(short_name, names_in[i] ? names_in[i] : "", sizeof(short_name));
        short_name[sizeof(short_name) - 1] = '\0';
        char *paren = strchr(short_name, '(');
        if (paren) {
            *paren = '\0';
            for (int k = strlen(short_name) - 1; k >= 0 && short_name[k] == ' '; k--) short_name[k] = '\0';
        }
        
        cairo_text_extents(cr, short_name, &te);
        cairo_translate(cr, bx + barW / 2.0, gy + gh + 12);
        cairo_rotate(cr, 0.6);
        cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
        cairo_move_to(cr, 0, 0);
        cairo_show_text(cr, short_name);
        cairo_restore(cr);
    }

    cairo_restore(cr);
}

// PAGE 2: GRAPHS
static void draw_graphs_page(cairo_t *cr, const MetricsData *metrics) {
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    cairo_set_source_rgb(cr, 0.1, 0.2, 0.4);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 24.0);
    cairo_move_to(cr, 40.0, 60.0);
    cairo_show_text(cr, "Graphs and Visualizations");

    double v_enc[4] = {0}, v_dec[4] = {0}, v_tp[4] = {0};
    const char *names[4] = {"", "", "", ""};

    for (size_t i = 0; i < 4 && i < metrics->count; i++) {
        v_enc[i] = metrics->results[i].encrypt_ms;
        v_dec[i] = metrics->results[i].decrypt_ms;
        v_tp[i] = metrics->results[i].throughput_mb_s;
        names[i] = metrics->names[i];
    }

    const double cw = 440.0;
    const double ch = 200.0;
    const double cx = (PAGE_W - cw) / 2.0;

    draw_pdf_bar_chart(cr, cx, 100.0, cw, ch, "Encryption Time", v_enc, names, "ms");
    draw_pdf_bar_chart(cr, cx, 330.0, cw, ch, "Decryption Time", v_dec, names, "ms");
    draw_pdf_bar_chart(cr, cx, 560.0, cw, ch, "Throughput", v_tp, names, "MB/s");
}

void generate_pdf_report(
    const char *input_filename,
    MetricsData *metrics
) {
    if (!metrics) return;
    metrics->success = 0;
    if (!metrics->results || !metrics->names || metrics->count == 0) return;

    char out_pdf[320];
    make_report_filename(input_filename, out_pdf, sizeof(out_pdf));
    
    // Create cairo PDF surface
    cairo_surface_t *surface = cairo_pdf_surface_create(out_pdf, PAGE_W, PAGE_H);
    if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        if (surface) cairo_surface_destroy(surface);
        return;
    }

    cairo_t *cr = cairo_create(surface);
    if (!cr || cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        if (cr) cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return;
    }

    // Page 1: Summary Table
    draw_summary_page(cr, input_filename ? basename_ptr(input_filename) : "Unknown", metrics);
    cairo_show_page(cr);

    // Page 2: Graphs
    draw_graphs_page(cr, metrics);
    cairo_show_page(cr);

    cairo_surface_finish(surface);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    metrics->success = 1;
}
