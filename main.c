// main.c - Demo for lightweight IDMC-inspired stream cipher
#include "gui.h"

static void app_activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;
    GtkWidget *win = gui_create_window(app);
    gtk_widget_show_all(win);
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("capstone.encryption.evaluation", G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(app_activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}

