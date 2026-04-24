// gui.h
#ifndef GUI_H
#define GUI_H

#include <gtk/gtk.h>
#include <stdint.h>
#include <stddef.h>

typedef void* (*algo_init_fn)(const uint8_t *key, size_t key_len, const uint8_t *nonce, size_t nonce_len, int enc);
typedef void (*algo_process_fn)(void *ctx, const uint8_t *in, uint8_t *out, size_t len);
typedef void (*algo_cleanup_fn)(void *ctx);

typedef struct {
    const char *name;
    algo_init_fn init;
    algo_process_fn encrypt;
    algo_process_fn decrypt;
    algo_cleanup_fn cleanup;
} Algorithm;

typedef struct {
    GtkWidget *text_view;
    GtkTextBuffer *buffer;
} AppWidgets;

GtkWidget *gui_create_window(GtkApplication *app);

#endif
