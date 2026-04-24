// gui.c
#include "gui.h"
#include "metrics.h"
#include "export.h"
#include "pdf.h"
#include "aes.h"
#include "chacha20.h"
#include "present.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char current_file_path[512] = "";
char current_file_name[256] = "manual_input.txt";

// Algorithm implementations are expected to be provided in algorithms/*.c
void idmc_encrypt(const uint8_t*, uint8_t*, size_t, const uint8_t*, size_t, const uint8_t*, size_t);
void idmc_decrypt(const uint8_t*, uint8_t*, size_t, const uint8_t*, size_t, const uint8_t*, size_t);

typedef struct {
    double current_values_enc[4];
    double current_values_dec[4];
    double current_values_tp[4];
    double current_values_mem[4];
    double target_values_enc[4];
    double target_values_dec[4];
    double target_values_tp[4];
    double target_values_mem[4];
    gboolean animating;
    double alpha;
    int tick_count;
    guint timer_id;
} GraphAnimation;

typedef struct {
    GtkWidget *window;

    AppWidgets input;
    AppWidgets output;

    GtkWidget *run_btn;
    GtkWidget *load_btn;
    GtkWidget *clear_btn;
    GtkWidget *export_btn;
    GtkWidget *save_graph_btn;
    GtkWidget *report_btn;

    GtkListStore *store;
    GtkWidget *tree;

    GtkWidget *charts_area;

    GraphAnimation anim;

    GMutex lock;
    EvalMetrics results[4];
    char *names[4];
    int have_results;

    GThread *worker;
    int worker_running;
    int is_alive;
} AppUI;

enum {
    COL_ALGO = 0,
    COL_ENC_MS,
    COL_DEC_MS,
    COL_TP,
    COL_MEM,
    N_COLS
};

static void append_text(GtkTextBuffer *buffer, const char *text) {
    if (!buffer) {
        printf("Buffer NULL\n");
        return;
    }
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, text ? text : "", -1);
}

static void append_output(AppUI *ui, const char *text) {
    if (!ui || !text || !ui->is_alive) return;
    if (!ui->output.buffer) {
        printf("Buffer NULL\n");
        return;
    }
    if (!ui->output.text_view || !GTK_IS_TEXT_VIEW(ui->output.text_view)) return;

    append_text(ui->output.buffer, text);
    append_text(ui->output.buffer, "\n");

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(ui->output.buffer, &end);
    GtkTextMark *mark = gtk_text_buffer_create_mark(ui->output.buffer, NULL, &end, FALSE);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(ui->output.text_view), mark);
    gtk_text_buffer_delete_mark(ui->output.buffer, mark);
}

static void set_buttons_enabled(AppUI *ui, int enabled) {
    if (!ui || !ui->is_alive) return;
    gtk_widget_set_sensitive(ui->run_btn, enabled);
    gtk_widget_set_sensitive(ui->load_btn, enabled);
    gtk_widget_set_sensitive(ui->clear_btn, enabled);
    gtk_widget_set_sensitive(ui->export_btn, enabled);
    gtk_widget_set_sensitive(ui->save_graph_btn, enabled);
    gtk_widget_set_sensitive(ui->report_btn, enabled);
}

static char *read_file_or_null(const char *path, gsize *out_len) {
    gchar *contents = NULL;
    gsize len = 0;
    GError *err = NULL;
    if (!g_file_get_contents(path, &contents, &len, &err)) {
        if (err) g_error_free(err);
        return NULL;
    }
    if (out_len) *out_len = len;
    return (char *)contents; // must be freed with g_free
}

static void set_input_text(AppUI *ui, const char *text) {
    if (!ui || !ui->is_alive) return;
    if (!ui->input.buffer) {
        printf("Buffer NULL\n");
        return;
    }
    gtk_text_buffer_set_text(ui->input.buffer, text ? text : "", -1);
}

static char *get_input_text(AppUI *ui) {
    if (!ui || !ui->is_alive) return g_strdup("");
    if (!ui->input.buffer) {
        printf("Buffer NULL\n");
        return g_strdup("");
    }
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(ui->input.buffer, &start, &end);
    return gtk_text_buffer_get_text(ui->input.buffer, &start, &end, FALSE); // g_free
}

static void clear_output_cb(GtkButton *btn, gpointer user_data) {
    (void)btn;
    AppUI *ui = (AppUI *)user_data;
    if (!ui || !ui->is_alive || !ui->output.buffer) return;
    gtk_text_buffer_set_text(ui->output.buffer, "", -1);
}

static int copy_results_snapshot(AppUI *ui, EvalMetrics out_results[4], char *out_names[4]) {
    if (!ui || !ui->is_alive) return 0;
    for (int i = 0; i < 4; i++) out_names[i] = NULL;

    g_mutex_lock(&ui->lock);
    if (!ui->have_results) {
        g_mutex_unlock(&ui->lock);
        return 0;
    }
    for (int i = 0; i < 4; i++) {
        out_results[i] = ui->results[i];
        out_names[i] = g_strdup(ui->names[i] ? ui->names[i] : "Unknown");
    }
    g_mutex_unlock(&ui->lock);
    return 1;
}

static void free_snapshot_names(char *names[4]) {
    for (int i = 0; i < 4; i++) {
        g_free(names[i]);
        names[i] = NULL;
    }
}

static const char *basename_ptr(const char *path) {
    const char *base = path;
    const char *p = path;
    while (p && *p) {
        if (*p == '/' || *p == '\\') base = p + 1;
        p++;
    }
    return base;
}

static void make_report_filename(const char *source_name, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!source_name || !source_name[0]) {
        snprintf(out, out_size, "report.pdf");
        return;
    }

    const char *base = basename_ptr(source_name);
    char stem[256];
    strncpy(stem, base, sizeof(stem) - 1U);
    stem[sizeof(stem) - 1U] = '\0';

    char *dot = strrchr(stem, '.');
    if (dot) *dot = '\0';
    if (stem[0] == '\0') snprintf(stem, sizeof(stem), "report");

    snprintf(out, out_size, "%s_report.pdf", stem);
}

static void export_results_cb(GtkButton *btn, gpointer user_data) {
    (void)btn;
    AppUI *ui = (AppUI *)user_data;
    EvalMetrics results[4];
    char *names[4];
    if (!copy_results_snapshot(ui, results, names)) {
        append_output(ui, "Error saving file");
        append_output(ui, "Run evaluation first.");
        return;
    }

    const char *const_names[4] = { names[0], names[1], names[2], names[3] };
    if (export_csv("results.csv", results, const_names, 4U)) {
        append_output(ui, "Export Successful");
    } else {
        append_output(ui, "Error saving file");
    }
    free_snapshot_names(names);
}

static void save_graph_cb(GtkButton *btn, gpointer user_data) {
    (void)btn;
    AppUI *ui = (AppUI *)user_data;
    EvalMetrics results[4];
    char *names[4];
    if (!copy_results_snapshot(ui, results, names)) {
        append_output(ui, "Error saving file");
        append_output(ui, "Run evaluation first.");
        return;
    }

    const char *const_names[4] = { names[0], names[1], names[2], names[3] };
    const int width = gtk_widget_get_allocated_width(ui->charts_area);
    const int height = gtk_widget_get_allocated_height(ui->charts_area);
    if (export_graph("results.png", results, const_names, 4U, width > 0 ? width : 620, height > 0 ? height : 240)) {
        append_output(ui, "Export Successful");
    } else {
        append_output(ui, "Error saving file");
    }
    free_snapshot_names(names);
}

static void generate_report_cb(GtkButton *btn, gpointer user_data) {
    (void)btn;
    AppUI *ui = (AppUI *)user_data;
    EvalMetrics results[4];
    char *names[4];
    if (!copy_results_snapshot(ui, results, names)) {
        append_output(ui, "Error saving file");
        append_output(ui, "Run evaluation first.");
        return;
    }

    const char *const_names[4] = { names[0], names[1], names[2], names[3] };
    const int width = gtk_widget_get_allocated_width(ui->charts_area);
    const int height = gtk_widget_get_allocated_height(ui->charts_area);
    if (!export_graph("results.png", results, const_names, 4U, width > 0 ? width : 620, height > 0 ? height : 240)) {
        append_output(ui, "ERROR: Could not generate PDF");
        free_snapshot_names(names);
        return;
    }

    MetricsData md;
    md.results = results;
    md.names = const_names;
    md.count = 4U;
    md.source_file = current_file_name;
    md.success = 0;

    generate_pdf_report(current_file_name, &md);
    if (md.success) {
        append_output(ui, "PDF Generated Successfully");
    } else {
        append_output(ui, "ERROR: Could not generate PDF");
    }
    free_snapshot_names(names);
}

static void load_data_cb(GtkButton *btn, gpointer user_data) {
    (void)btn;
    AppUI *ui = (AppUI *)user_data;

    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Select Data File",
        GTK_WINDOW(ui->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        NULL
    );

    GtkFileFilter *filter_json = gtk_file_filter_new();
    gtk_file_filter_set_name(filter_json, "JSON files");
    gtk_file_filter_add_pattern(filter_json, "*.json");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter_json);

    GtkFileFilter *filter_csv = gtk_file_filter_new();
    gtk_file_filter_set_name(filter_csv, "CSV files");
    gtk_file_filter_add_pattern(filter_csv, "*.csv");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter_csv);

    GtkFileFilter *filter_all = gtk_file_filter_new();
    gtk_file_filter_set_name(filter_all, "All files");
    gtk_file_filter_add_pattern(filter_all, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter_all);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *selected = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (selected) {
            FILE *fp = fopen(selected, "rb");
            if (!fp) {
                append_output(ui, "ERROR: Could not load selected file");
            } else {
                if (fseek(fp, 0, SEEK_END) == 0) {
                    long flen = ftell(fp);
                    if (flen >= 0 && fseek(fp, 0, SEEK_SET) == 0) {
                        size_t sz = (size_t)flen;
                        char *buf = (char *)malloc(sz + 1U);
                        if (buf) {
                            size_t read_n = fread(buf, 1, sz, fp);
                            buf[read_n] = '\0';
                            set_input_text(ui, buf);
                            free(buf);

                            strncpy(current_file_path, selected, sizeof(current_file_path) - 1U);
                            current_file_path[sizeof(current_file_path) - 1U] = '\0';
                            strncpy(current_file_name, basename_ptr(selected), sizeof(current_file_name) - 1U);
                            current_file_name[sizeof(current_file_name) - 1U] = '\0';

                            append_output(ui, "File loaded successfully.");
                        } else {
                            append_output(ui, "ERROR: Could not load selected file");
                        }
                    } else {
                        append_output(ui, "ERROR: Could not load selected file");
                    }
                } else {
                    append_output(ui, "ERROR: Could not load selected file");
                }
                fclose(fp);
            }
            g_free(selected);
        }
    }

    gtk_widget_destroy(dialog);
}

typedef struct {
    AppUI *ui;
    char *log_line;
} UiLogMsg;

static gboolean ui_append_log_idle(gpointer user_data) {
    UiLogMsg *m = (UiLogMsg *)user_data;
    if (m && m->ui && m->ui->is_alive) append_output(m->ui, m->log_line);
    g_free(m->log_line);
    g_free(m);
    return G_SOURCE_REMOVE;
}

static void ui_log_async(AppUI *ui, const char *line) {
    if (!ui || !ui->is_alive || !line) return;
    UiLogMsg *m = (UiLogMsg *)g_malloc0(sizeof(*m));
    m->ui = ui;
    m->log_line = g_strdup(line);
    g_idle_add(ui_append_log_idle, m);
}

typedef struct {
    AppUI *ui;
    EvalMetrics results[4];
    char *names[4];
} UiResultsMsg;

static void store_set_row(AppUI *ui, int idx, const char *name, const EvalMetrics *m) {
    GtkTreeIter it;
    gtk_list_store_append(ui->store, &it);

    char enc[64], dec[64], tp[64], mem[64];
    snprintf(enc, sizeof(enc), "%.3f ms", m->encrypt_ms);
    snprintf(dec, sizeof(dec), "%.3f ms", m->decrypt_ms);
    snprintf(tp, sizeof(tp), "%.2f MB/s", m->throughput_mb_s);
    snprintf(mem, sizeof(mem), "%zu B", m->memory_bytes);

    gtk_list_store_set(ui->store, &it,
        COL_ALGO, name,
        COL_ENC_MS, enc,
        COL_DEC_MS, dec,
        COL_TP, tp,
        COL_MEM, mem,
        -1
    );
    (void)idx;
}

static gboolean animate_graph(gpointer user_data) {
    AppUI *ui = (AppUI *)user_data;
    if (!ui || !ui->is_alive || !ui->anim.animating) {
        if (ui && ui->is_alive) {
            ui->anim.animating = FALSE;
            ui->anim.timer_id = 0;
        }
        return G_SOURCE_REMOVE;
    }

    g_mutex_lock(&ui->lock);
    
    ui->anim.tick_count++;
    ui->anim.alpha += 0.05;
    if (ui->anim.alpha > 1.0) ui->anim.alpha = 1.0;
    
    gboolean still_animating = FALSE;
    
    for (int i = 0; i < 4; i++) {
        if (ui->anim.tick_count > i * 5) {
            double ease = 0.08;
            
            double d_enc = ui->anim.target_values_enc[i] - ui->anim.current_values_enc[i];
            ui->anim.current_values_enc[i] += d_enc * ease;
            if (d_enc > 0.005 || d_enc < -0.005) still_animating = TRUE;
            
            double d_dec = ui->anim.target_values_dec[i] - ui->anim.current_values_dec[i];
            ui->anim.current_values_dec[i] += d_dec * ease;
            if (d_dec > 0.005 || d_dec < -0.005) still_animating = TRUE;
            
            double d_tp = ui->anim.target_values_tp[i] - ui->anim.current_values_tp[i];
            ui->anim.current_values_tp[i] += d_tp * ease;
            if (d_tp > 0.005 || d_tp < -0.005) still_animating = TRUE;
            
            double d_mem = ui->anim.target_values_mem[i] - ui->anim.current_values_mem[i];
            ui->anim.current_values_mem[i] += d_mem * ease;
            if (d_mem > 0.005 || d_mem < -0.005) still_animating = TRUE;
        } else {
            still_animating = TRUE;
        }
    }
    
    if (!still_animating || ui->anim.tick_count > 180) {
        for(int i=0; i<4; i++) {
            ui->anim.current_values_enc[i] = ui->anim.target_values_enc[i];
            ui->anim.current_values_dec[i] = ui->anim.target_values_dec[i];
            ui->anim.current_values_tp[i]  = ui->anim.target_values_tp[i];
            ui->anim.current_values_mem[i] = ui->anim.target_values_mem[i];
        }
        ui->anim.animating = FALSE;
        ui->anim.timer_id = 0;
        g_mutex_unlock(&ui->lock);
        gtk_widget_queue_draw(ui->charts_area);
        return G_SOURCE_REMOVE;
    }
    
    g_mutex_unlock(&ui->lock);
    gtk_widget_queue_draw(ui->charts_area);
    return G_SOURCE_CONTINUE;
}

static gboolean ui_apply_results_idle(gpointer user_data) {
    UiResultsMsg *msg = (UiResultsMsg *)user_data;
    AppUI *ui = msg->ui;
    if (!ui || !ui->is_alive) {
        for (int i = 0; i < 4; i++) g_free(msg->names[i]);
        g_free(msg);
        return G_SOURCE_REMOVE;
    }

    gtk_list_store_clear(ui->store);
    for (int i = 0; i < 4; i++) {
        store_set_row(ui, i, msg->names[i], &msg->results[i]);
    }

    g_mutex_lock(&ui->lock);
    for (int i = 0; i < 4; i++) {
        ui->results[i] = msg->results[i];
        g_free(ui->names[i]);
        ui->names[i] = g_strdup(msg->names[i]);
        
        ui->anim.target_values_enc[i] = msg->results[i].encrypt_ms;
        ui->anim.target_values_dec[i] = msg->results[i].decrypt_ms;
        ui->anim.target_values_tp[i]  = msg->results[i].throughput_mb_s;
        ui->anim.target_values_mem[i] = (double)msg->results[i].memory_bytes;
        
        ui->anim.current_values_enc[i] = 0.0;
        ui->anim.current_values_dec[i] = 0.0;
        ui->anim.current_values_tp[i]  = 0.0;
        ui->anim.current_values_mem[i] = 0.0;
    }
    ui->have_results = 1;

    ui->anim.animating = TRUE;
    ui->anim.alpha = 0.0;
    ui->anim.tick_count = 0;
    
    if (ui->anim.timer_id != 0) {
        g_source_remove(ui->anim.timer_id);
    }
    ui->anim.timer_id = g_timeout_add(16, animate_graph, ui);

    g_mutex_unlock(&ui->lock);

    ui->worker_running = 0;
    if (ui->worker) {
        g_thread_unref(ui->worker);
        ui->worker = NULL;
    }
    set_buttons_enabled(ui, 1);

    for (int i = 0; i < 4; i++) g_free(msg->names[i]);
    g_free(msg);
    return G_SOURCE_REMOVE;
}

static void apply_results_async(AppUI *ui, const EvalMetrics results[4], char *names[4]) {
    UiResultsMsg *msg = (UiResultsMsg *)g_malloc0(sizeof(*msg));
    msg->ui = ui;
    for (int i = 0; i < 4; i++) {
        msg->results[i] = results[i];
        msg->names[i] = names[i];
    }
    g_idle_add(ui_apply_results_idle, msg);
}

static void draw_bar_chart(cairo_t *cr,
                           double x, double y, double w, double h,
                           const char *title,
                           const double target_v[4],
                           const double current_v[4],
                           const char *names[4],
                           const char *unit,
                           double alpha) {
    cairo_save(cr);
    cairo_push_group(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_SUBPIXEL);

    cairo_set_source_rgb(cr, 0.12, 0.12, 0.12);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 0.30, 0.30, 0.30);
    cairo_rectangle(cr, x + 0.5, y + 0.5, w - 1.0, h - 1.0);
    cairo_stroke(cr);

    cairo_select_font_face(cr, "Segoe UI", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 14.0);
    cairo_set_source_rgb(cr, 0.95, 0.95, 0.95);
    cairo_move_to(cr, x + 15, y + 25);
    cairo_show_text(cr, title);

    double sorted_v[4];
    for (int i = 0; i < 4; i++) sorted_v[i] = target_v[i];
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
    const double padT = 35.0; 
    const double padB = 45.0; 
    const double gx = x + padL;
    const double gy = y + padT;
    const double gw = w - padL - padR;
    const double gh = h - padT - padB;

    if (gw < 10 || gh < 10) { 
        cairo_restore(cr); 
        return; 
    }

    cairo_set_source_rgb(cr, 0.16, 0.16, 0.16);
    cairo_rectangle(cr, gx, gy, gw, gh);
    cairo_fill(cr);

    cairo_save(cr);
    cairo_select_font_face(cr, "Segoe UI", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12.0);
    cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
    cairo_translate(cr, x + 18, gy + gh / 2.0);
    cairo_rotate(cr, -1.57079632679);
    cairo_text_extents_t te;
    char ylabel[64];
    snprintf(ylabel, sizeof(ylabel), "Value (%s)", unit);
    cairo_text_extents(cr, ylabel, &te);
    cairo_move_to(cr, -te.width / 2.0, 0);
    cairo_show_text(cr, ylabel);
    cairo_restore(cr);

    cairo_set_line_width(cr, 1.0);
    cairo_select_font_face(cr, "Segoe UI", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10.0);
    for (int i = 0; i <= 4; i++) {
        double line_y = gy + gh - (i * gh / 4.0);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
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
        cairo_set_source_rgb(cr, 0.75, 0.75, 0.75);
        cairo_move_to(cr, gx - te.width - 8, line_y + 4);
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
        double tv = target_v[i];
        double bv = current_v[i];
        int capped_this = 0;
        if (tv > maxv_drawn && is_capped) {
            if (bv >= maxv_drawn) bv = maxv_drawn;
            capped_this = 1;
        }

        double bh = (bv / maxv_drawn) * gh;
        if (bh < 2.0 && current_v[i] > 0.0) bh = 2.0; 
        double bx = gx + barGap + i * (barW + barGap);
        double by = gy + (gh - bh);

        cairo_set_source_rgb(cr, colors[i][0], colors[i][1], colors[i][2]);
        cairo_rectangle(cr, bx, by, barW, bh);
        cairo_fill(cr);

        if (capped_this) {
            cairo_set_source_rgb(cr, 0.16, 0.16, 0.16);
            cairo_move_to(cr, bx - 1, by + 4);
            cairo_line_to(cr, bx + barW/2, by - 2);
            cairo_line_to(cr, bx + barW + 1, by + 4);
            cairo_line_to(cr, bx + barW + 1, by - 2);
            cairo_line_to(cr, bx - 1, by - 2);
            cairo_fill(cr);
        }

        char val_label[64];
        if (current_v[i] >= 100.0) snprintf(val_label, sizeof(val_label), "%.0f %s", current_v[i], unit);
        else if (current_v[i] >= 10.0) snprintf(val_label, sizeof(val_label), "%.1f %s", current_v[i], unit);
        else snprintf(val_label, sizeof(val_label), "%.2f %s", current_v[i], unit);

        cairo_select_font_face(cr, "Segoe UI", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 11.0);
        cairo_text_extents(cr, val_label, &te);

        double tx = bx + (barW - te.width) / 2.0;
        double ty = by - 8.0;
        cairo_set_source_rgb(cr, 0.95, 0.95, 0.95);
        cairo_move_to(cr, tx, ty);
        cairo_show_text(cr, val_label);

        cairo_save(cr);
        cairo_select_font_face(cr, "Segoe UI", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 12.0);
        
        char short_name[64];
        strncpy(short_name, names[i] ? names[i] : "", sizeof(short_name));
        short_name[sizeof(short_name) - 1] = '\0';
        char *paren = strchr(short_name, '(');
        if (paren) {
            *paren = '\0';
            for (int k = strlen(short_name) - 1; k >= 0 && short_name[k] == ' '; k--) {
                short_name[k] = '\0';
            }
        }
        
        cairo_text_extents(cr, short_name, &te);
        cairo_translate(cr, bx + barW / 2.0, gy + gh + 14);
        cairo_rotate(cr, 0.6);
        cairo_set_source_rgb(cr, 0.85, 0.85, 0.85);
        cairo_move_to(cr, 0, 0);
        cairo_show_text(cr, short_name);
        cairo_restore(cr);
    }

    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, alpha);
    cairo_restore(cr);
}

static gboolean charts_draw_cb(GtkWidget *w, cairo_t *cr, gpointer user_data) {
    (void)w;
    AppUI *ui = (AppUI *)user_data;

    int width = gtk_widget_get_allocated_width(ui->charts_area);
    int height = gtk_widget_get_allocated_height(ui->charts_area);

    cairo_set_source_rgb(cr, 0.10, 0.10, 0.10);
    cairo_paint(cr);

    const char *names[4] = { "IDMC", "AES (OpenSSL - Standard)", "ChaCha20 (OpenSSL - Standard)", "PRESENT (Real Implementation)" };
    char *dyn_names[4] = { NULL, NULL, NULL, NULL };

    gboolean anim_state = FALSE;
    double alpha = 1.0;
    
    double t_enc[4] = {0}, t_dec[4] = {0}, t_tp[4] = {0}, t_mem[4] = {0};
    double c_enc[4] = {0}, c_dec[4] = {0}, c_tp[4] = {0}, c_mem[4] = {0};

    g_mutex_lock(&ui->lock);
    if (ui->have_results) {
        for (int i = 0; i < 4; i++) {
            dyn_names[i] = ui->names[i] ? g_strdup(ui->names[i]) : NULL;
            if (dyn_names[i]) names[i] = dyn_names[i];
            
            t_enc[i] = ui->anim.target_values_enc[i];
            t_dec[i] = ui->anim.target_values_dec[i];
            t_tp[i]  = ui->anim.target_values_tp[i];
            t_mem[i] = ui->anim.target_values_mem[i];
            
            c_enc[i] = ui->anim.current_values_enc[i];
            c_dec[i] = ui->anim.current_values_dec[i];
            c_tp[i]  = ui->anim.current_values_tp[i];
            c_mem[i] = ui->anim.current_values_mem[i];
        }
        if (ui->anim.animating) {
            alpha = ui->anim.alpha;
        }
    }
    g_mutex_unlock(&ui->lock);

    const double pad = 10.0;
    const double cellW = (width - (3.0 * pad)) / 2.0;
    const double cellH = (height - (3.0 * pad)) / 2.0;

    draw_bar_chart(cr, pad, pad, cellW, cellH, "Encryption Time", t_enc, c_enc, names, "ms", alpha);
    draw_bar_chart(cr, pad * 2 + cellW, pad, cellW, cellH, "Decryption Time", t_dec, c_dec, names, "ms", alpha);
    draw_bar_chart(cr, pad, pad * 2 + cellH, cellW, cellH, "Throughput", t_tp, c_tp, names, "MB/s", alpha);
    draw_bar_chart(cr, pad * 2 + cellW, pad * 2 + cellH, cellW, cellH, "Memory Usage", t_mem, c_mem, names, "B", alpha);

    for (int i = 0; i < 4; i++) g_free(dyn_names[i]);
    return FALSE;
}

typedef struct {
    AppUI *ui;
    uint8_t *data;
    size_t len;
} WorkerArgs;

static void fill_key_nonce(uint8_t *key, size_t key_len, uint8_t *nonce, size_t nonce_len) {
    // Deterministic-ish seed per run to keep comparisons consistent.
    // Not cryptographically strong; just for evaluation UI.
    for (size_t i = 0; i < key_len; i++) key[i] = (uint8_t)(0xA5 ^ (uint8_t)i);
    for (size_t i = 0; i < nonce_len; i++) nonce[i] = (uint8_t)(0x5A ^ (uint8_t)(i * 3U));
}

typedef struct {
    const uint8_t *key;   size_t klen;
    const uint8_t *nonce; size_t nlen;
} idmc_ctx_t;

static void* idmc_init(const uint8_t *key, size_t key_len, const uint8_t *nonce, size_t nonce_len, int enc) {
    (void)enc;
    idmc_ctx_t *ctx = malloc(sizeof(idmc_ctx_t));
    if (!ctx) return NULL;
    ctx->key = key; ctx->klen = key_len;
    ctx->nonce = nonce; ctx->nlen = nonce_len;
    return ctx;
}
static void idmc_encrypt_eval(void *ctx, const uint8_t *in, uint8_t *out, size_t len) {
    idmc_ctx_t *c = ctx;
    idmc_encrypt(in, out, len, c->key, c->klen, c->nonce, c->nlen);
}
static void idmc_decrypt_eval(void *ctx, const uint8_t *in, uint8_t *out, size_t len) {
    idmc_ctx_t *c = ctx;
    idmc_decrypt(in, out, len, c->key, c->klen, c->nonce, c->nlen);
}
static void idmc_cleanup(void *ctx) { free(ctx); }

static uint8_t demo_aes_key[16] = "0123456789abcdef";
static uint8_t demo_aes_iv[16]  = "abcdef9876543210";
static void* aes_init_eval(const uint8_t *k, size_t kl, const uint8_t *n, size_t nl, int enc) {
    (void)k; (void)kl; (void)n; (void)nl;
    return init_aes_context(demo_aes_key, 16, demo_aes_iv, 16, enc);
}

static uint8_t demo_chacha_key[32] = "0123456789abcdef0123456789abcdef";
static uint8_t demo_chacha_nonce[12] = "abcdef987654";
static void* chacha_init_eval(const uint8_t *k, size_t kl, const uint8_t *n, size_t nl, int enc) {
    (void)k; (void)kl; (void)n; (void)nl;
    return init_chacha20_context(demo_chacha_key, 32, demo_chacha_nonce, 12, enc);
}

static gpointer worker_thread(gpointer user_data) {
    WorkerArgs *args = (WorkerArgs *)user_data;
    AppUI *ui = args->ui;

    Algorithm algos[4] = {
        { "IDMC", idmc_init, idmc_encrypt_eval, idmc_decrypt_eval, idmc_cleanup },
        { "AES (OpenSSL - Standard)", aes_init_eval, aes_encrypt_update, aes_encrypt_update, aes_cleanup },
        { "ChaCha20 (OpenSSL - Standard)", chacha_init_eval, chacha20_process, chacha20_process, chacha20_cleanup },
        { "PRESENT (Real Implementation)", present_init, present_process, present_process, present_cleanup }
    };

    uint8_t key[32];
    uint8_t nonce[16];
    fill_key_nonce(key, sizeof(key), nonce, sizeof(nonce));

    EvalMetrics res[4];
    char *names[4] = { NULL, NULL, NULL, NULL };

    ui_log_async(ui, "=== Encryption Evaluation System ===");
    ui_log_async(ui, "Loading input bytes and running all algorithms...");

    for (int i = 0; i < 4; i++) {
        char line[256];
        snprintf(line, sizeof(line), "=== Running %s ===", algos[i].name);
        ui_log_async(ui, line);

        metrics_run_eval(algos[i].name, algos[i].init, algos[i].encrypt, algos[i].decrypt, algos[i].cleanup,
                         args->data, args->len, key, sizeof(key), nonce, sizeof(nonce),
                         &res[i]);

        snprintf(line, sizeof(line), "Encryption Time: %.3f ms", res[i].encrypt_ms);
        ui_log_async(ui, line);
        snprintf(line, sizeof(line), "Decryption Time: %.3f ms", res[i].decrypt_ms);
        ui_log_async(ui, line);
        snprintf(line, sizeof(line), "Throughput: %.2f MB/s", res[i].throughput_mb_s);
        ui_log_async(ui, line);
        snprintf(line, sizeof(line), "Memory Usage: %zu B", res[i].memory_bytes);
        ui_log_async(ui, line);
        ui_log_async(ui, res[i].decrypt_ok ? "Decryption Successful" : "Decryption FAILED");
        ui_log_async(ui, "");

        names[i] = g_strdup(algos[i].name);
    }

    apply_results_async(ui, res, names);

    free(args->data);
    free(args);
    return NULL;
}

static void run_eval_cb(GtkButton *btn, gpointer user_data) {
    (void)btn;
    AppUI *ui = (AppUI *)user_data;
    if (!ui || !ui->is_alive) return;
    if (ui->worker_running) return;

    uint8_t *bytes = NULL;
    size_t len = 0;
    char *txt = get_input_text(ui); // g_free
    const size_t tlen = txt ? strlen(txt) : 0;
    bytes = (uint8_t *)malloc(tlen ? tlen : 1);
    if (bytes && tlen) memcpy(bytes, txt, tlen);
    len = tlen;
    g_free(txt);

    if (current_file_name[0] == '\0') {
        strncpy(current_file_name, "manual_input.txt", sizeof(current_file_name) - 1U);
        current_file_name[sizeof(current_file_name) - 1U] = '\0';
    }

    if (!bytes || len == 0) {
        free(bytes);
        append_output(ui, "ERROR: No input data to evaluate.");
        return;
    }

    gtk_list_store_clear(ui->store);
    set_buttons_enabled(ui, 0);
    ui->worker_running = 1;

    WorkerArgs *args = (WorkerArgs *)malloc(sizeof(*args));
    args->ui = ui;
    args->data = bytes;
    args->len = len;

    ui->worker = g_thread_new("eval-worker", worker_thread, args);
}

static GtkWidget *make_scrolled_textview(AppWidgets *out_widgets, int monospace, int readonly) {
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    GtkWidget *tv = gtk_text_view_new();
    if (!tv || !GTK_IS_TEXT_VIEW(tv)) {
        printf("TextView NULL or invalid\n");
        return sw;
    }
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv));
    if (!buf) {
        printf("Buffer NULL\n");
        return sw;
    }
    gtk_container_add(GTK_CONTAINER(sw), tv);

    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), readonly ? FALSE : TRUE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(tv), readonly ? FALSE : TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(tv), 8);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(tv), 8);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv), monospace ? TRUE : FALSE);

    if (out_widgets) {
        out_widgets->text_view = tv;
        out_widgets->buffer = buf;
    }
    return sw;
}

static GtkWidget *build_tree(AppUI *ui) {
    ui->store = gtk_list_store_new(N_COLS,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING
    );
    GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(ui->store));
    ui->tree = tree;

    const char *titles[N_COLS] = { "Algorithm", "Encrypt Time", "Decrypt Time", "Throughput", "Memory Usage" };
    for (int i = 0; i < N_COLS; i++) {
        GtkCellRenderer *r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(titles[i], r, "text", i, NULL);
        gtk_tree_view_column_set_resizable(col, TRUE);
        gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree), col);
    }

    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sw), tree);
    return sw;
}

static void apply_css_dark(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    const char *css =
        "* {"
        "  font-family: 'Segoe UI', 'Sans';"
        "}"
        "window { background: #111; }"
        "textview, treeview {"
        "  background: #161616;"
        "  color: #e8e8e8;"
        "}"
        "button {"
        "  background: #242424;"
        "  color: #eaeaea;"
        "  border: 1px solid #333;"
        "  padding: 6px 10px;"
        "}"
        "button:hover { background: #2b2b2b; }"
        ".header {"
        "  background: #0f0f0f;"
        "  border-bottom: 1px solid #2a2a2a;"
        "}"
        ".header label {"
        "  font-size: 22px;"
        "  font-weight: 700;"
        "  color: #f0f0f0;"
        "}"
        ;
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static void app_ui_destroy(gpointer data) {
    AppUI *ui = (AppUI *)data;
    if (!ui) return;
    ui->is_alive = 0;
    if (ui->worker) {
        g_thread_join(ui->worker);
        ui->worker = NULL;
    }
    ui->input.text_view = NULL;
    ui->input.buffer = NULL;
    ui->output.text_view = NULL;
    ui->output.buffer = NULL;
    for (int i = 0; i < 4; i++) {
        g_free(ui->names[i]);
        ui->names[i] = NULL;
    }
    g_mutex_clear(&ui->lock);
    g_free(ui);
}

GtkWidget *gui_create_window(GtkApplication *app) {
    AppUI *ui = (AppUI *)g_malloc0(sizeof(*ui));
    g_mutex_init(&ui->lock);
    ui->is_alive = 1;

    GtkSettings *settings = gtk_settings_get_default();
    if (settings) g_object_set(settings, "gtk-application-prefer-dark-theme", TRUE, NULL);
    apply_css_dark();

    GtkWidget *win = gtk_application_window_new(app);
    ui->window = win;
    gtk_window_set_title(GTK_WINDOW(win), "Encryption Evaluation System");
    gtk_window_set_default_size(GTK_WINDOW(win), 1200, 700);
    gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(win), root);

    // Header
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(header), "header");
    gtk_widget_set_hexpand(header, TRUE);
    gtk_widget_set_size_request(header, -1, 54);
    gtk_box_pack_start(GTK_BOX(root), header, FALSE, FALSE, 0);

    GtkWidget *title = gtk_label_new("Encryption Evaluation System");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_widget_set_margin_start(title, 14);
    gtk_widget_set_margin_top(title, 10);
    gtk_widget_set_margin_bottom(title, 10);
    gtk_box_pack_start(GTK_BOX(header), title, TRUE, TRUE, 0);

    // Middle split
    GtkWidget *mid_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(root), mid_paned, TRUE, TRUE, 0);

    // Left panel: input + buttons
    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(left, 10);
    gtk_widget_set_margin_bottom(left, 10);
    gtk_widget_set_margin_start(left, 10);
    gtk_widget_set_margin_end(left, 5);

    GtkWidget *input_sw = make_scrolled_textview(&ui->input, 0, 0);
    gtk_widget_set_vexpand(input_sw, TRUE);
    gtk_box_pack_start(GTK_BOX(left), input_sw, TRUE, TRUE, 0);

    GtkWidget *btn_row_top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *btn_row_bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(left), btn_row_top, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left), btn_row_bottom, FALSE, FALSE, 0);

    ui->load_btn = gtk_button_new_with_label("Load Data");
    ui->run_btn = gtk_button_new_with_label("Run Evaluation");
    ui->export_btn = gtk_button_new_with_label("Export Results");
    ui->save_graph_btn = gtk_button_new_with_label("Save Graph");
    ui->report_btn = gtk_button_new_with_label("Generate Report");
    ui->clear_btn = gtk_button_new_with_label("Clear Output");
    gtk_box_pack_start(GTK_BOX(btn_row_top), ui->load_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btn_row_top), ui->run_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btn_row_top), ui->export_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btn_row_bottom), ui->save_graph_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btn_row_bottom), ui->report_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btn_row_bottom), ui->clear_btn, TRUE, TRUE, 0);

    g_signal_connect(ui->load_btn, "clicked", G_CALLBACK(load_data_cb), ui);
    g_signal_connect(ui->run_btn, "clicked", G_CALLBACK(run_eval_cb), ui);
    g_signal_connect(ui->export_btn, "clicked", G_CALLBACK(export_results_cb), ui);
    g_signal_connect(ui->save_graph_btn, "clicked", G_CALLBACK(save_graph_cb), ui);
    g_signal_connect(ui->report_btn, "clicked", G_CALLBACK(generate_report_cb), ui);
    g_signal_connect(ui->clear_btn, "clicked", G_CALLBACK(clear_output_cb), ui);

    // Right panel: output console
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(right, 10);
    gtk_widget_set_margin_bottom(right, 10);
    gtk_widget_set_margin_start(right, 5);
    gtk_widget_set_margin_end(right, 10);

    GtkWidget *output_sw = make_scrolled_textview(&ui->output, 1, 1);
    gtk_widget_set_vexpand(output_sw, TRUE);
    gtk_box_pack_start(GTK_BOX(right), output_sw, TRUE, TRUE, 0);

    gtk_paned_pack1(GTK_PANED(mid_paned), left, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(mid_paned), right, TRUE, FALSE);
    gtk_paned_set_position(GTK_PANED(mid_paned), 520);

    // Bottom panel: table + graphs
    GtkWidget *bottom = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_start(bottom, 10);
    gtk_widget_set_margin_end(bottom, 10);
    gtk_widget_set_margin_bottom(bottom, 10);
    gtk_widget_set_margin_top(bottom, 0);
    gtk_box_pack_start(GTK_BOX(root), bottom, FALSE, TRUE, 0);

    GtkWidget *tree_sw = build_tree(ui);
    gtk_widget_set_size_request(tree_sw, 520, 240);
    gtk_paned_pack1(GTK_PANED(bottom), tree_sw, TRUE, FALSE);

    ui->charts_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(ui->charts_area, 700, 500);
    g_signal_connect(ui->charts_area, "draw", G_CALLBACK(charts_draw_cb), ui);
    gtk_paned_pack2(GTK_PANED(bottom), ui->charts_area, TRUE, FALSE);
    gtk_paned_set_position(GTK_PANED(bottom), 560);

    // Seed input with a hint if file missing
    set_input_text(ui, "{\n  \"sample\": \"Paste JSON here or click Load Data\"\n}\n");
    append_output(ui, "Ready. Click Load Data or Run Evaluation.");

    // Keep ui alive via window data
    g_object_set_data_full(G_OBJECT(win), "app-ui", ui, (GDestroyNotify)app_ui_destroy);

    return win;
}

