#include "window.h"
static void activate(GtkApplication *app, gpointer data) {
    (void)data;
    md_window_present(app);
}
int main(int argc, char **argv) {
    g_autoptr(GtkApplication) app =
        gtk_application_new("io.github.fernandorojas.MediaDownloader", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    return g_application_run(G_APPLICATION(app), argc, argv);
}
