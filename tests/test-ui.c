#include "window.h"
#include "preferences.h"
#include <string.h>
static GtkApplication *app;
static guint step, ticks;
static gboolean update_started, update_finished;
static const gchar *url, *screenshot;
static GtkWidget *find_widget(GtkWidget *root, GType type, const gchar *text) {
    if (g_type_is_a(G_OBJECT_TYPE(root), type)) {
        const gchar *value = GTK_IS_LABEL(root)    ? gtk_label_get_text(GTK_LABEL(root))
                             : GTK_IS_BUTTON(root) ? gtk_button_get_label(GTK_BUTTON(root))
                                                   : NULL;
        if (!text || (value && (GTK_IS_BUTTON(root) ? g_strcmp0(value, text) == 0
                                                    : strstr(value, text) != NULL)))
            return root;
    }
    for (GtkWidget *child = gtk_widget_get_first_child(root); child;
         child = gtk_widget_get_next_sibling(child)) {
        GtkWidget *found = find_widget(child, type, text);
        if (found)
            return found;
    }
    return NULL;
}
static GtkWidget *theme_button(GtkWidget *root) {
    if (GTK_IS_BUTTON(root) &&
        g_strcmp0(gtk_widget_get_tooltip_text(root), "Alternar tema claro y oscuro") == 0)
        return root;
    for (GtkWidget *child = gtk_widget_get_first_child(root); child;
         child = gtk_widget_get_next_sibling(child)) {
        GtkWidget *found = theme_button(child);
        if (found)
            return found;
    }
    return NULL;
}
static gboolean dark_appearance(GtkWindow *window) {
    GdkRGBA background;
    g_assert_true(gtk_style_context_lookup_color(gtk_widget_get_style_context(GTK_WIDGET(window)),
                                                 "theme_bg_color", &background));
    return 0.2126 * background.red + 0.7152 * background.green + 0.0722 * background.blue < 0.5;
}
static guint theme_checks;
static gboolean initial_dark;
static void capture(GtkWindow *window) {
    if (!screenshot)
        return;
    GtkWidget *widget = gtk_window_get_child(window);
    gint width = gtk_widget_get_width(widget), height = gtk_widget_get_height(widget);
    g_autoptr(GdkPaintable) paintable = gtk_widget_paintable_new(widget);
    GtkSnapshot *snapshot = gtk_snapshot_new();
    GdkRGBA background = {1, 1, 1, 1};
    gtk_style_context_lookup_color(gtk_widget_get_style_context(GTK_WIDGET(window)),
                                   "theme_bg_color", &background);
    graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, width, height);
    gtk_snapshot_append_color(snapshot, &background, &bounds);
    gdk_paintable_snapshot(paintable, GDK_SNAPSHOT(snapshot), width, height);
    GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
    g_assert_nonnull(node);
    GskRenderer *renderer = gtk_native_get_renderer(GTK_NATIVE(window));
    g_autoptr(GdkTexture) texture = gsk_renderer_render_texture(renderer, node, NULL);
    g_assert_true(gdk_texture_save_to_png(texture, screenshot));
    gsk_render_node_unref(node);
}
static gboolean drive(gpointer data) {
    (void)data;
    ticks++;
    guint timeout = g_getenv("MD_UI_TIMEOUT")
                        ? (guint)g_ascii_strtoull(g_getenv("MD_UI_TIMEOUT"), NULL, 10) * 10
                        : 900;
    if (ticks > timeout)
        g_error("UI integration timed out at step %u", step);
    GtkWindow *window = gtk_application_get_active_window(app);
    g_assert_nonnull(window);
    g_assert_cmpstr(gtk_window_get_icon_name(window), ==,
                    "io.github.fernandorojas.MediaDownloader");
    g_assert_true(gtk_icon_theme_has_icon(
        gtk_icon_theme_get_for_display(gtk_widget_get_display(GTK_WIDGET(window))),
        "io.github.fernandorojas.MediaDownloader"));
    GtkWidget *root = GTK_WIDGET(window);
    if (theme_checks < 3) {
        GtkWidget *toggle = theme_button(root);
        g_assert_nonnull(toggle);
        if (theme_checks == 0) {
            initial_dark = dark_appearance(window);
            MdPreferences *saved = md_preferences_load();
            if (g_strcmp0(saved->theme, "system") != 0)
                g_assert_cmpint(initial_dark, ==, g_str_equal(saved->theme, "dark"));
            md_preferences_free(saved);
        } else {
            gboolean expected = theme_checks == 1 ? !initial_dark : initial_dark;
            g_assert_cmpint(dark_appearance(window), ==, expected);
            MdPreferences *saved = md_preferences_load();
            g_assert_cmpstr(saved->theme, ==, expected ? "dark" : "light");
            md_preferences_free(saved);
        }
        if (theme_checks < 2)
            g_signal_emit_by_name(toggle, "clicked");
        theme_checks++;
        return G_SOURCE_CONTINUE;
    }
    if (g_getenv("MD_THEME_ONLY")) {
        capture(window);
        step = 3;
        gtk_window_close(window);
        return G_SOURCE_REMOVE;
    }
    if (step == 0) {
        if (g_getenv("MD_UI_UPDATE_ENGINE") && !update_finished) {
            GtkWidget *update = find_widget(root, GTK_TYPE_BUTTON, "Actualizar yt-dlp");
            if (!update_started) {
                if (!update || !gtk_widget_get_visible(update) || !gtk_widget_get_sensitive(update))
                    return G_SOURCE_CONTINUE;
                g_signal_emit_by_name(update, "clicked");
                update_started = TRUE;
            } else if (find_widget(root, GTK_TYPE_LABEL, "Última versión estable"))
                update_finished = TRUE;
            return G_SOURCE_CONTINUE;
        }
        GtkWidget *analyze = find_widget(root, GTK_TYPE_BUTTON, "Analizar");
        if (!gtk_widget_get_sensitive(analyze))
            return G_SOURCE_CONTINUE;
        GtkWidget *download = find_widget(root, GTK_TYPE_BUTTON, "Descargar");
        g_assert_false(gtk_widget_get_sensitive(download));
        GtkWidget *entry = find_widget(root, GTK_TYPE_ENTRY, NULL);
        gtk_editable_set_text(GTK_EDITABLE(entry), url);
        g_signal_emit_by_name(analyze, "clicked");
        step++;
    } else if (step == 1) {
        GtkWidget *download = find_widget(root, GTK_TYPE_BUTTON, "Descargar");
        if (!gtk_widget_get_sensitive(download))
            return G_SOURCE_CONTINUE;
        const gchar *expected = g_getenv("MD_UI_TITLE");
        g_assert_nonnull(find_widget(root, GTK_TYPE_LABEL, expected ? expected : "own-test"));
        if (g_getenv("MD_UI_AUDIO")) {
            GtkWidget *format = find_widget(root, GTK_TYPE_DROP_DOWN, NULL);
            gtk_drop_down_set_selected(GTK_DROP_DOWN(format), 3);
            g_assert_true(gtk_widget_get_sensitive(download));
        }
        g_signal_emit_by_name(download, "clicked");
        step++;
    } else if (step == 2) {
        if (!find_widget(root, GTK_TYPE_LABEL, "Completado"))
            return G_SOURCE_CONTINUE;
        GtkWidget *picture = find_widget(root, GTK_TYPE_PICTURE, NULL);
        if (!gtk_picture_get_paintable(GTK_PICTURE(picture)))
            return G_SOURCE_CONTINUE;
        step++;
    } else if (step == 3) {
        capture(window);
        gtk_window_close(window);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}
static void activate(GtkApplication *application, gpointer data) {
    (void)data;
    md_window_present(application);
    g_timeout_add(100, drive, NULL);
}
int main(int argc, char **argv) {
    if (argc < 3)
        return 2;
    url = argv[1];
    screenshot = argc > 3 ? argv[3] : NULL;
    MdPreferences *prefs = md_preferences_load();
    g_free(prefs->directory);
    prefs->directory = g_strdup(argv[2]);
    prefs->notify = FALSE;
    g_autoptr(GError) error = NULL;
    g_assert_true(md_preferences_save(prefs, &error));
    md_preferences_free(prefs);
    app = gtk_application_new("io.github.fernandorojas.MediaDownloader.Tests",
                              G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    gint result = g_application_run(G_APPLICATION(app), 1, argv);
    g_object_unref(app);
    g_assert_cmpuint(step, ==, 3);
    return result;
}
