#include "window.h"
#include "download-manager.h"
#include "preferences.h"
#include "utils.h"
#include "updates.h"
#include "config.h"
#include <glib/gstdio.h>
typedef struct {
    GtkWidget *box, *title, *state, *detail, *progress, *cancel, *retry, *remove;
    MdJob *job;
    gboolean notified;
} Row;
typedef struct {
    GtkApplication *app;
    GtkWidget *window, *url, *analyze, *spinner, *message, *title, *meta, *picture, *placeholder,
        *format, *quality, *hint, *destination, *download, *playlist, *list, *counter,
        *engine_update_label, *app_update_label, *engine_update_button, *app_update_button,
        *check_updates_button;
    MdPreferences *prefs;
    MdManager *manager;
    MdVideo *video;
    GCancellable *analysis_cancel, *thumbnail_cancel, *updates_cancel;
    MdUpdates *updates;
    GArray *heights;
    GPtrArray *checks;
    GtkNativeDialog *chooser;
    guint pending;
    gboolean dependencies, closing, checking_updates, updating;
    gchar *analyzed_url;
} Window;
static void refresh_update_controls(Window *w);
static void check_updates(Window *w);
static void clear_video(Window *w);
static void dependencies_done(GObject *source, GAsyncResult *result, gpointer data);
static const gchar *containers[] = {"mp4",  "webm", "mkv", "mp3", "m4a",
                                    "opus", "flac", "wav", NULL};
static const gchar *container_labels[] = {"Video · MP4",  "Video · WebM", "Video · MKV",
                                          "Audio · MP3",  "Audio · M4A",  "Audio · Opus",
                                          "Audio · FLAC", "Audio · WAV",  NULL};
static void message(Window *w, const gchar *text) {
    gtk_label_set_text(GTK_LABEL(w->message), text ? text : "");
}
static GtkWidget *label(const gchar *text, const gchar *style) {
    GtkWidget *l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_label_set_wrap(GTK_LABEL(l), TRUE);
    if (style)
        gtk_widget_add_css_class(l, style);
    return l;
}
static GtkWidget *box(GtkOrientation orientation, gint spacing) {
    return gtk_box_new(orientation, spacing);
}
static GtkWidget *icon_button(const gchar *icon, const gchar *tip) {
    GtkWidget *b = gtk_button_new_from_icon_name(icon);
    gtk_widget_set_tooltip_text(b, tip);
    return b;
}
static void margins(GtkWidget *widget, gint size) {
    gtk_widget_set_margin_start(widget, size);
    gtk_widget_set_margin_end(widget, size);
    gtk_widget_set_margin_top(widget, size);
    gtk_widget_set_margin_bottom(widget, size);
}
static guint container_index(const gchar *name) {
    for (guint i = 0; containers[i]; i++)
        if (g_strcmp0(name, containers[i]) == 0)
            return i;
    return 0;
}
static gint selected_height(Window *w) {
    guint n = gtk_drop_down_get_selected(GTK_DROP_DOWN(w->quality));
    return w->heights && n < w->heights->len ? g_array_index(w->heights, gint, n) : 0;
}
static const gchar *selected_container(Window *w) {
    guint n = gtk_drop_down_get_selected(GTK_DROP_DOWN(w->format));
    return n < 8 ? containers[n] : "mp4";
}
static void update_selection(Window *w) {
    gboolean ready = w->video && w->dependencies && !w->closing && !w->updating;
    gtk_widget_set_sensitive(w->format, ready);
    gtk_widget_set_sensitive(w->quality,
                             ready && gtk_drop_down_get_selected(GTK_DROP_DOWN(w->format)) < 3);
    if (ready && w->video->entries->len == 0) {
        g_autoptr(GError) error = NULL;
        g_autoptr(MdSelection) s =
            md_select_format(w->video, selected_container(w), selected_height(w), &error);
        g_autofree gchar *hint =
            s && s->audio_language
                ? g_strdup_printf("%s · Idioma del audio: %s", md_processing_name(s->processing),
                                  s->audio_language)
                : NULL;
        gtk_label_set_text(GTK_LABEL(w->hint), hint ? hint
                                               : s  ? md_processing_name(s->processing)
                                                    : error->message);
        ready = s != NULL;
        if (ready && s->audio && !md_audio_conversion_supported(s->container)) {
            gtk_label_set_text(GTK_LABEL(w->hint),
                               "FFmpeg no dispone del codificador necesario para este audio.");
            ready = FALSE;
        }
    } else
        gtk_label_set_text(GTK_LABEL(w->hint),
                           ready ? "Cada elemento se analizará antes de descargar. Confirma tu "
                                   "selección al añadirla a la cola."
                                 : "Analiza una URL para seleccionar los formatos disponibles.");
    gtk_widget_set_sensitive(w->download, ready);
}
static void selection_changed(GObject *object, GParamSpec *spec, gpointer data) {
    (void)object;
    (void)spec;
    update_selection(data);
}
static void open_folder_path(Window *w, const gchar *path) {
    g_autofree gchar *uri = g_filename_to_uri(path, NULL, NULL);
    if (uri)
        gtk_show_uri(GTK_WINDOW(w->window), uri, GDK_CURRENT_TIME);
}
static void row_cancel(GtkButton *b, gpointer data) {
    (void)b;
    md_job_cancel(((Row *)data)->job);
}
static void row_retry(GtkButton *b, gpointer data) {
    (void)b;
    md_job_retry(((Row *)data)->job);
}
static void row_open(GtkButton *b, gpointer data) {
    (void)b;
    MdJob *j = ((Row *)data)->job;
    open_folder_path(j->manager->data, j->directory);
}
static void row_remove(GtkButton *b, gpointer data) {
    (void)b;
    Row *r = data;
    MdJob *j = r->job;
    Window *w = j->manager->data;
    GtkWidget *widget = r->box;
    if (!md_state_terminal(j->state))
        return;
    gtk_box_remove(GTK_BOX(w->list), widget);
    j->ui = NULL;
    md_manager_remove(w->manager, j);
    g_free(r);
    g_autofree gchar *text =
        g_strdup_printf("%u elementos · %u activos", w->manager->jobs->len, w->manager->active);
    gtk_label_set_text(GTK_LABEL(w->counter), text);
}
static void job_changed(MdJob *j, gpointer data) {
    Window *w = data;
    refresh_update_controls(w);
    Row *r = j->ui;
    if (!r)
        return;
    gtk_label_set_text(GTK_LABEL(r->state), md_state_name(j->state));
    gtk_label_set_text(GTK_LABEL(r->detail), j->error    ? j->error
                                             : j->detail ? j->detail
                                                         : "Esperando información de progreso…");
    if (j->fraction >= 0) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(r->progress), j->fraction);
        g_autofree gchar *text = g_strdup_printf("%.1f %%", 100 * j->fraction);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(r->progress), text);
    } else {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(r->progress), 0);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(r->progress), md_state_name(j->state));
    }
    gtk_widget_set_sensitive(r->cancel, !md_state_terminal(j->state));
    gtk_widget_set_visible(r->retry, j->state == MD_ERROR || j->state == MD_CANCELLED);
    gtk_widget_set_visible(r->remove, md_state_terminal(j->state));
    g_autofree gchar *counter =
        g_strdup_printf("%u elementos · %u activos", w->manager->jobs->len, w->manager->active);
    gtk_label_set_text(GTK_LABEL(w->counter), counter);
    if (j->state == MD_COMPLETED && !r->notified && !w->closing) {
        r->notified = TRUE;
        if (w->prefs->notify) {
            g_autoptr(GNotification) n = g_notification_new("Descarga completada");
            g_notification_set_body(n, j->title);
            g_application_send_notification(G_APPLICATION(w->app), NULL, n);
        }
        if (w->prefs->open_folder)
            open_folder_path(w, j->directory);
    }
}
static void add_job(Window *w, MdVideo *v) {
    MdJob *j = md_manager_add(w->manager, v->url, v->title, w->prefs->directory,
                              selected_container(w), selected_height(w));
    Row *r = g_new0(Row, 1);
    r->job = j;
    j->ui = r;
    r->box = box(GTK_ORIENTATION_VERTICAL, 8);
    margins(r->box, 14);
    gtk_widget_add_css_class(r->box, "card");
    GtkWidget *header = box(GTK_ORIENTATION_HORIZONTAL, 10);
    r->title = label(v->title, "heading");
    gtk_label_set_ellipsize(GTK_LABEL(r->title), PANGO_ELLIPSIZE_END);
    gtk_label_set_wrap(GTK_LABEL(r->title), FALSE);
    gtk_widget_set_hexpand(r->title, TRUE);
    gtk_box_append(GTK_BOX(header), r->title);
    r->cancel = icon_button("process-stop-symbolic", "Cancelar");
    r->retry = icon_button("view-refresh-symbolic", "Reintentar");
    r->remove = icon_button("edit-delete-symbolic", "Quitar de la lista sin borrar archivos");
    GtkWidget *folder = icon_button("folder-open-symbolic", "Abrir carpeta");
    gtk_box_append(GTK_BOX(header), folder);
    gtk_box_append(GTK_BOX(header), r->cancel);
    gtk_box_append(GTK_BOX(header), r->retry);
    gtk_box_append(GTK_BOX(header), r->remove);
    r->state = label("En espera", "accent");
    r->progress = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(r->progress), TRUE);
    r->detail = label("", "dim-label");
    gtk_box_append(GTK_BOX(r->box), header);
    gtk_box_append(GTK_BOX(r->box), r->state);
    gtk_box_append(GTK_BOX(r->box), r->progress);
    gtk_box_append(GTK_BOX(r->box), r->detail);
    gtk_box_append(GTK_BOX(w->list), r->box);
    g_signal_connect(r->cancel, "clicked", G_CALLBACK(row_cancel), r);
    g_signal_connect(r->retry, "clicked", G_CALLBACK(row_retry), r);
    g_signal_connect(r->remove, "clicked", G_CALLBACK(row_remove), r);
    g_signal_connect(folder, "clicked", G_CALLBACK(row_open), r);
    job_changed(j, w);
}
static void enqueue_selected(Window *w) {
    if (!w->video)
        return;
    g_autoptr(GError) error = NULL;
    if (!md_directory_valid(w->prefs->directory, &error)) {
        message(w, error->message);
        return;
    }
    if (!w->video->entries->len)
        add_job(w, w->video);
    else
        for (guint i = 0; i < w->checks->len; i++)
            if (gtk_check_button_get_active(GTK_CHECK_BUTTON(g_ptr_array_index(w->checks, i))))
                add_job(w, g_ptr_array_index(w->video->entries, i));
    md_manager_schedule(w->manager);
    message(w, "Descargas añadidas a la cola.");
}
static void confirm_playlist(GtkDialog *dialog, gint response, gpointer data) {
    Window *w = data;
    if (response == GTK_RESPONSE_ACCEPT && !w->closing)
        enqueue_selected(w);
    gtk_window_destroy(GTK_WINDOW(dialog));
}
static void download_clicked(GtkButton *b, gpointer data) {
    (void)b;
    Window *w = data;
    if (!w->video || !w->dependencies || w->updating)
        return;
    if (!w->video->entries->len) {
        enqueue_selected(w);
        return;
    }
    guint count = 0;
    for (guint i = 0; i < w->checks->len; i++)
        if (gtk_check_button_get_active(GTK_CHECK_BUTTON(g_ptr_array_index(w->checks, i))))
            count++;
    if (!count) {
        message(w, "Selecciona al menos un elemento de la lista.");
        return;
    }
    GtkWidget *d = gtk_message_dialog_new(
        GTK_WINDOW(w->window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "¿Añadir %u elementos a la cola?", count);
    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(d), "Confirma que tienes autorización para descargar estos elementos. "
                               "Se comprobarán los formatos de cada uno.");
    gtk_dialog_add_buttons(GTK_DIALOG(d), "Cancelar", GTK_RESPONSE_CANCEL, "Añadir a la cola",
                           GTK_RESPONSE_ACCEPT, NULL);
    g_signal_connect(d, "response", G_CALLBACK(confirm_playlist), w);
    gtk_window_present(GTK_WINDOW(d));
}
static void select_all(GtkCheckButton *b, gpointer data) {
    Window *w = data;
    for (guint i = 0; i < w->checks->len; i++)
        gtk_check_button_set_active(GTK_CHECK_BUTTON(g_ptr_array_index(w->checks, i)),
                                    gtk_check_button_get_active(b));
}
typedef struct {
    Window *w;
    gchar *dir;
    MdProcess *child;
    GCancellable *cancel;
    gulong handler;
} Thumbnail;
static void thumbnail_cancelled(GCancellable *cancel, gpointer data) {
    (void)cancel;
    md_process_cancel(((Thumbnail *)data)->child);
}
static void thumbnail_done(gboolean ok, const gchar *error, gpointer data) {
    (void)error;
    Thumbnail *t = data;
    md_process_free(t->child);
    if (t->handler)
        g_cancellable_disconnect(t->cancel, t->handler);
    g_autofree gchar *file = g_build_filename(t->dir, "cover.png", NULL);
    if (ok && !t->w->closing && !g_cancellable_is_cancelled(t->cancel)) {
        g_autoptr(GdkTexture) texture = gdk_texture_new_from_filename(file, NULL);
        if (texture) {
            gtk_picture_set_paintable(GTK_PICTURE(t->w->picture), GDK_PAINTABLE(texture));
            gtk_widget_set_visible(t->w->placeholder, FALSE);
        }
    }
    GDir *dir = g_dir_open(t->dir, 0, NULL);
    if (dir) {
        const gchar *name;
        while ((name = g_dir_read_name(dir))) {
            g_autofree gchar *path = g_build_filename(t->dir, name, NULL);
            g_unlink(path);
        }
        g_dir_close(dir);
    }
    g_rmdir(t->dir);
    t->w->pending--;
    refresh_update_controls(t->w);
    g_object_unref(t->cancel);
    g_free(t->dir);
    g_free(t);
}
static void load_thumbnail(Window *w) {
    if (!w->video->thumbnail || !md_url_valid(w->video->url))
        return;
    Thumbnail *t = g_new0(Thumbnail, 1);
    t->dir = g_dir_make_tmp("media-downloader-thumbnail-XXXXXX", NULL);
    if (!t->dir) {
        g_free(t);
        return;
    }
    g_autofree gchar *output = g_build_filename(t->dir, "cover.%(ext)s", NULL);
    const gchar *argv[] = {"yt-dlp",
                           "--ignore-config",
                           "--no-playlist",
                           "--skip-download",
                           "--write-thumbnail",
                           "--convert-thumbnails",
                           "png",
                           "--socket-timeout",
                           "10",
                           "--retries",
                           "1",
                           "-o",
                           output,
                           "--",
                           w->video->url,
                           NULL};
    t->child = md_process_start(argv, NULL, thumbnail_done, t, NULL);
    if (!t->child) {
        g_rmdir(t->dir);
        g_free(t->dir);
        g_free(t);
        return;
    }
    t->w = w;
    t->cancel = g_object_ref(w->thumbnail_cancel);
    t->handler = g_cancellable_connect(t->cancel, G_CALLBACK(thumbnail_cancelled), t, NULL);
    w->pending++;
}
static void analysis_done(MdVideo *video, const gchar *error, gpointer data) {
    Window *w = data;
    w->pending--;
    refresh_update_controls(w);
    gtk_spinner_stop(GTK_SPINNER(w->spinner));
    gtk_widget_set_sensitive(w->analyze, w->dependencies && !w->closing);
    if (w->closing) {
        md_video_free(video);
        return;
    }
    if (!video) {
        g_autofree gchar *detail = md_error_message(error);
        message(w, detail);
        update_selection(w);
        return;
    }
    const gchar *current_url = gtk_editable_get_text(GTK_EDITABLE(w->url));
    if (g_strcmp0(current_url, w->analyzed_url) != 0) {
        md_video_free(video);
        message(w, "La URL cambió durante el análisis. Analiza la nueva dirección.");
        return;
    }
    w->video = video;
    if (!md_url_valid(video->url)) {
        g_free(video->url);
        video->url = g_strdup(w->analyzed_url);
    }
    gtk_label_set_text(GTK_LABEL(w->title), video->title);
    g_autofree gchar *duration = md_duration(video->duration);
    g_autofree gchar *meta = g_strdup_printf(
        "%s · %s · %s · %u formatos", video->author ? video->author : "Autor no disponible",
        duration, video->site ? video->site : "Sitio no disponible", video->formats->len);
    gtk_label_set_text(GTK_LABEL(w->meta), meta);
    GtkStringList *qualities = gtk_string_list_new(NULL);
    gtk_string_list_append(qualities, "Mejor calidad disponible");
    g_array_set_size(w->heights, 0);
    gint zero = 0;
    g_array_append_val(w->heights, zero);
    g_autoptr(GArray) heights = md_resolutions(video);
    guint preferred = 0;
    for (guint i = 0; i < heights->len; i++) {
        gint h = g_array_index(heights, gint, i);
        g_autofree gchar *q = g_strdup_printf("%dp", h);
        gtk_string_list_append(qualities, q);
        g_array_append_val(w->heights, h);
        if (h == w->prefs->quality)
            preferred = i + 1;
    }
    gtk_drop_down_set_model(GTK_DROP_DOWN(w->quality), G_LIST_MODEL(qualities));
    g_object_unref(qualities);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(w->quality), preferred);
    gtk_widget_set_visible(w->playlist, video->entries->len > 0);
    if (video->entries->len) {
        GtkWidget *all = gtk_check_button_new_with_label("Seleccionar todos");
        gtk_box_append(GTK_BOX(w->playlist), all);
        g_signal_connect(all, "toggled", G_CALLBACK(select_all), w);
        g_autofree gchar *count = g_strdup_printf(
            "%u elementos disponibles. El análisis está limitado a los primeros 100 elementos.",
            video->entries->len);
        gtk_box_append(GTK_BOX(w->playlist), label(count, "dim-label"));
        for (guint i = 0; i < video->entries->len; i++) {
            MdVideo *v = g_ptr_array_index(video->entries, i);
            g_autofree gchar *d = md_duration(v->duration);
            g_autofree gchar *text = g_strdup_printf("%s · %s", v->title, d);
            GtkWidget *check = gtk_check_button_new_with_label(text);
            gtk_box_append(GTK_BOX(w->playlist), check);
            g_ptr_array_add(w->checks, check);
        }
    } else
        load_thumbnail(w);
    message(w, "Análisis completado.");
    update_selection(w);
}
static void clear_video(Window *w) {
    g_clear_pointer(&w->video, md_video_free);
    gtk_label_set_text(GTK_LABEL(w->title), "Tu próxima descarga");
    gtk_label_set_text(GTK_LABEL(w->meta), "Título, autor, duración y formatos aparecerán aquí.");
    gtk_picture_set_paintable(GTK_PICTURE(w->picture), NULL);
    gtk_widget_set_visible(w->placeholder, TRUE);
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(w->playlist)))
        gtk_box_remove(GTK_BOX(w->playlist), child);
    g_ptr_array_set_size(w->checks, 0);
    gtk_widget_set_visible(w->playlist, FALSE);
}
static void analyze_clicked(GtkButton *b, gpointer data) {
    (void)b;
    Window *w = data;
    const gchar *url = gtk_editable_get_text(GTK_EDITABLE(w->url));
    if (!md_url_valid(url)) {
        message(w, "Introduce una URL HTTP o HTTPS válida, sin espacios.");
        return;
    }
    clear_video(w);
    g_cancellable_cancel(w->thumbnail_cancel);
    g_clear_object(&w->thumbnail_cancel);
    w->thumbnail_cancel = g_cancellable_new();
    g_cancellable_reset(w->analysis_cancel);
    g_free(w->analyzed_url);
    w->analyzed_url = g_strdup(url);
    gtk_widget_set_sensitive(w->analyze, FALSE);
    gtk_spinner_start(GTK_SPINNER(w->spinner));
    message(w, "Analizando contenido…");
    update_selection(w);
    w->pending++;
    md_analyze_async(url, w->analysis_cancel, analysis_done, w);
}
static void url_changed(GtkEditable *entry, gpointer data) {
    (void)entry;
    Window *w = data;
    if (w->video) {
        clear_video(w);
        update_selection(w);
    }
}
static void pasted(GObject *source, GAsyncResult *result, gpointer data) {
    Window *w = data;
    g_autofree gchar *text = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(source), result, NULL);
    w->pending--;
    refresh_update_controls(w);
    if (text && !w->closing)
        gtk_editable_set_text(GTK_EDITABLE(w->url), g_strstrip(text));
}
static void paste_clicked(GtkButton *b, gpointer data) {
    (void)b;
    Window *w = data;
    w->pending++;
    gdk_clipboard_read_text_async(gtk_widget_get_clipboard(w->window), w->analysis_cancel, pasted,
                                  w);
}
static void folder_response(GtkNativeDialog *dialog, gint response, gpointer data) {
    Window *w = data;
    w->chooser = NULL;
    w->pending--;
    refresh_update_controls(w);
    if (response == GTK_RESPONSE_ACCEPT && !w->closing) {
        g_autoptr(GFile) file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        gchar *path = g_file_get_path(file);
        if (path) {
            g_free(w->prefs->directory);
            w->prefs->directory = path;
            gtk_button_set_label(GTK_BUTTON(w->destination), path);
            GtkWidget *button = g_object_get_data(G_OBJECT(dialog), "source-button");
            if (button)
                gtk_button_set_label(GTK_BUTTON(button), path);
            g_autoptr(GError) error = NULL;
            if (!md_preferences_save(w->prefs, &error))
                message(w, error->message);
        }
    }
    gtk_native_dialog_destroy(dialog);
    g_object_unref(dialog);
}
static void choose_folder(GtkButton *b, gpointer data) {
    Window *w = data;
    if (w->chooser) {
        gtk_native_dialog_show(w->chooser);
        return;
    }
    GtkFileChooserNative *dialog = gtk_file_chooser_native_new(
        "Carpeta de destino", GTK_WINDOW(w->window), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "Seleccionar", "Cancelar");
    g_autoptr(GFile) folder = g_file_new_for_path(w->prefs->directory);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), folder, NULL);
    w->chooser = GTK_NATIVE_DIALOG(dialog);
    w->pending++;
    g_object_set_data_full(G_OBJECT(dialog), "source-button", g_object_ref(b), g_object_unref);
    g_signal_connect(dialog, "response", G_CALLBACK(folder_response), w);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}
static void apply_theme(Window *w) {
    GtkSettings *settings = gtk_settings_get_default();
    if (!settings)
        return;
    if (g_str_equal(w->prefs->theme, "system"))
        gtk_settings_reset_property(settings, "gtk-application-prefer-dark-theme");
    else
        g_object_set(settings, "gtk-application-prefer-dark-theme",
                     g_str_equal(w->prefs->theme, "dark"), NULL);
}
typedef struct {
    Window *w;
    GtkWidget *window, *video, *audio, *quality, *concurrency, *theme, *notify, *open;
} Prefs;
static void save_prefs(GtkButton *button, gpointer data) {
    (void)button;
    Prefs *p = data;
    Window *w = p->w;
    const gchar *themes[] = {"system", "light", "dark"};
    const gchar *audio[] = {"mp3", "m4a", "opus", "flac", "wav"};
    const gint qualities[] = {0, 360, 480, 720, 1080, 1440, 2160};
    g_free(w->prefs->video);
    w->prefs->video = g_strdup(containers[gtk_drop_down_get_selected(GTK_DROP_DOWN(p->video))]);
    g_free(w->prefs->audio);
    w->prefs->audio = g_strdup(audio[gtk_drop_down_get_selected(GTK_DROP_DOWN(p->audio))]);
    g_free(w->prefs->theme);
    w->prefs->theme = g_strdup(themes[gtk_drop_down_get_selected(GTK_DROP_DOWN(p->theme))]);
    w->prefs->quality = qualities[gtk_drop_down_get_selected(GTK_DROP_DOWN(p->quality))];
    w->prefs->concurrency =
        (guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(p->concurrency));
    w->prefs->notify = gtk_check_button_get_active(GTK_CHECK_BUTTON(p->notify));
    w->prefs->open_folder = gtk_check_button_get_active(GTK_CHECK_BUTTON(p->open));
    g_autoptr(GError) error = NULL;
    if (!md_preferences_save(w->prefs, &error)) {
        message(w, error->message);
        return;
    }
    w->manager->limit = w->prefs->concurrency;
    apply_theme(w);
    md_manager_schedule(w->manager);
    gtk_window_destroy(GTK_WINDOW(p->window));
    message(w, "Preferencias guardadas.");
}
static GtkWidget *pref_line(GtkWidget *parent, const gchar *text, GtkWidget *control) {
    GtkWidget *line = box(GTK_ORIENTATION_HORIZONTAL, 16), *l = label(text, NULL);
    gtk_widget_set_hexpand(l, TRUE);
    gtk_box_append(GTK_BOX(line), l);
    gtk_box_append(GTK_BOX(line), control);
    gtk_box_append(GTK_BOX(parent), line);
    return control;
}
static void preferences_clicked(GtkButton *b, gpointer data) {
    (void)b;
    Window *w = data;
    Prefs *p = g_new0(Prefs, 1);
    p->w = w;
    p->window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(p->window), "Preferencias");
    gtk_window_set_transient_for(GTK_WINDOW(p->window), GTK_WINDOW(w->window));
    gtk_window_set_modal(GTK_WINDOW(p->window), TRUE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(p->window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(p->window), 520, 480);
    g_object_set_data_full(G_OBJECT(p->window), "preferences", p, g_free);
    GtkWidget *content = box(GTK_ORIENTATION_VERTICAL, 18);
    margins(content, 24);
    gtk_window_set_child(GTK_WINDOW(p->window), content);
    gtk_box_append(GTK_BOX(content), label("Preferencias", "title"));
    GtkWidget *folder = gtk_button_new_with_label(w->prefs->directory);
    pref_line(content, "Carpeta predeterminada", folder);
    g_signal_connect(folder, "clicked", G_CALLBACK(choose_folder), w);
    const gchar *videos[] = {"MP4", "WebM", "MKV", NULL},
                *audios[] = {"MP3", "M4A", "Opus", "FLAC", "WAV", NULL},
                *qualities[] =
                    {"Mejor disponible", "360p", "480p", "720p", "1080p", "1440p", "2160p", NULL},
                *themes[] = {"Sistema", "Claro", "Oscuro", NULL};
    p->video = pref_line(content, "Video preferido", gtk_drop_down_new_from_strings(videos));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(p->video), MIN(container_index(w->prefs->video), 2u));
    p->audio = pref_line(content, "Audio preferido", gtk_drop_down_new_from_strings(audios));
    guint audio_index = container_index(w->prefs->audio);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(p->audio), audio_index >= 3 ? audio_index - 3 : 0);
    p->quality = pref_line(content, "Calidad preferida", gtk_drop_down_new_from_strings(qualities));
    const gint heights[] = {0, 360, 480, 720, 1080, 1440, 2160};
    for (guint i = 0; i < 7; i++)
        if (heights[i] == w->prefs->quality)
            gtk_drop_down_set_selected(GTK_DROP_DOWN(p->quality), i);
    p->concurrency =
        pref_line(content, "Descargas simultáneas", gtk_spin_button_new_with_range(1, 8, 1));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(p->concurrency), w->prefs->concurrency);
    p->theme = pref_line(content, "Apariencia", gtk_drop_down_new_from_strings(themes));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(p->theme), g_str_equal(w->prefs->theme, "dark") ? 2
                                                        : g_str_equal(w->prefs->theme, "light")
                                                            ? 1
                                                            : 0);
    p->notify = gtk_check_button_new_with_label("Notificar al completar una descarga");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(p->notify), w->prefs->notify);
    gtk_box_append(GTK_BOX(content), p->notify);
    p->open = gtk_check_button_new_with_label("Abrir carpeta al completar");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(p->open), w->prefs->open_folder);
    gtk_box_append(GTK_BOX(content), p->open);
    GtkWidget *save = gtk_button_new_with_label("Guardar preferencias");
    gtk_widget_add_css_class(save, "suggested-action");
    gtk_box_append(GTK_BOX(content), save);
    g_signal_connect(save, "clicked", G_CALLBACK(save_prefs), p);
    gtk_window_present(GTK_WINDOW(p->window));
}
static void preferred_audio_clicked(GtkButton *b, gpointer data) {
    (void)b;
    Window *w = data;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(w->format), container_index(w->prefs->audio));
}
static void theme_clicked(GtkButton *b, gpointer data) {
    (void)b;
    Window *w = data;
    gboolean dark = FALSE;
    g_object_get(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", &dark, NULL);
    g_free(w->prefs->theme);
    w->prefs->theme = g_strdup(dark ? "light" : "dark");
    apply_theme(w);
    g_autoptr(GError) error = NULL;
    if (!md_preferences_save(w->prefs, &error))
        message(w, error->message);
}
static void about_clicked(GtkButton *b, gpointer data) {
    (void)b;
    Window *w = data;
    gtk_show_about_dialog(
        GTK_WINDOW(w->window), "program-name", "Media Downloader", "version", MD_APP_VERSION,
        "copyright", "© 2026 Fernando Rojas", "license-type", GTK_LICENSE_MIT_X11, "logo-icon-name",
        "io.github.fernandorojas.MediaDownloader", "comments",
        "Interfaz nativa para yt-dlp y FFmpeg. Descarga únicamente contenido autorizado.", NULL);
}
static gboolean await_close(gpointer data) {
    Window *w = data;
    if (w->pending || w->manager->active)
        return G_SOURCE_CONTINUE;
    for (guint i = 0; i < w->manager->jobs->len; i++) {
        MdJob *j = g_ptr_array_index(w->manager->jobs, i);
        g_free(j->ui);
    }
    md_manager_free(w->manager);
    md_preferences_free(w->prefs);
    md_video_free(w->video);
    g_object_unref(w->analysis_cancel);
    g_object_unref(w->thumbnail_cancel);
    g_object_unref(w->updates_cancel);
    md_updates_free(w->updates);
    g_array_unref(w->heights);
    g_ptr_array_unref(w->checks);
    g_free(w->analyzed_url);
    gtk_window_destroy(GTK_WINDOW(w->window));
    g_free(w);
    return G_SOURCE_REMOVE;
}
static gboolean close_requested(GtkWindow *window, gpointer data) {
    Window *w = data;
    if (!w->closing) {
        w->closing = TRUE;
        if (w->chooser)
            g_signal_emit_by_name(w->chooser, "response", GTK_RESPONSE_CANCEL);
        gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
        g_cancellable_cancel(w->analysis_cancel);
        g_cancellable_cancel(w->thumbnail_cancel);
        g_cancellable_cancel(w->updates_cancel);
        md_manager_stop(w->manager);
        g_timeout_add(50, await_close, w);
    }
    return TRUE;
}
static void dependencies_done(GObject *source, GAsyncResult *result, gpointer data) {
    (void)source;
    Window *w = data;
    w->pending--;
    refresh_update_controls(w);
    g_autoptr(GError) error = NULL;
    w->dependencies = md_dependencies_finish(result, &error);
    if (!w->closing) {
        gtk_widget_set_sensitive(w->analyze, w->dependencies);
        message(w, error ? error->message
                         : "yt-dlp y FFmpeg disponibles. Introduce una URL para comenzar.");
    }
}
static void refresh_update_controls(Window *w) {
    if (!w->engine_update_button)
        return;
    gtk_widget_set_sensitive(w->engine_update_button, !w->closing && !w->updating &&
                                                          !w->checking_updates && w->pending == 0 &&
                                                          w->manager->active == 0);
    gtk_widget_set_sensitive(w->check_updates_button,
                             !w->closing && !w->updating && !w->checking_updates);
}
static void updates_checked(GObject *source, GAsyncResult *result, gpointer data) {
    (void)source;
    Window *w = data;
    w->pending--;
    w->checking_updates = FALSE;
    g_autoptr(GError) error = NULL;
    MdUpdates *updates = md_updates_check_finish(result, &error);
    if (w->closing) {
        md_updates_free(updates);
        return;
    }
    md_updates_free(w->updates);
    w->updates = updates;
    gtk_widget_set_visible(w->engine_update_button, FALSE);
    gtk_widget_set_visible(w->app_update_button, FALSE);
    if (!updates) {
        gtk_label_set_text(GTK_LABEL(w->engine_update_label),
                           error ? error->message
                                 : "No se pudieron comprobar las actualizaciones.");
        gtk_label_set_text(GTK_LABEL(w->app_update_label),
                           "Puedes volver a comprobar desde la barra superior.");
        refresh_update_controls(w);
        return;
    }
    if (updates->engine_error)
        gtk_label_set_text(GTK_LABEL(w->engine_update_label),
                           "No se pudo consultar la última versión de yt-dlp. Comprueba la "
                           "conexión o inténtalo más tarde.");
    else if (updates->engine) {
        gboolean newer = md_version_newer(updates->installed_engine, updates->engine->version);
        g_autofree gchar *text =
            newer ? g_strdup_printf("yt-dlp %s · Disponible: %s",
                                    updates->installed_engine ? updates->installed_engine
                                                              : "no instalado",
                                    updates->engine->version)
                  : g_strdup_printf("yt-dlp %s · Última versión estable: %s",
                                    updates->installed_engine ? updates->installed_engine
                                                              : "no disponible",
                                    updates->engine->version);
        gtk_label_set_text(GTK_LABEL(w->engine_update_label), text);
        gtk_widget_set_visible(w->engine_update_button, newer);
        gtk_button_set_label(GTK_BUTTON(w->engine_update_button),
                             updates->installed_engine ? "Actualizar yt-dlp" : "Instalar yt-dlp");
    }
    if (updates->app_error)
        gtk_label_set_text(GTK_LABEL(w->app_update_label),
                           "No se pudieron consultar las versiones de la aplicación. Puedes volver "
                           "a comprobar más tarde.");
    else if (updates->app_unpublished)
        gtk_label_set_text(GTK_LABEL(w->app_update_label),
                           "Aplicación " MD_APP_VERSION
                           " · Aún no hay versiones publicadas en GitHub.");
    else if (updates->app) {
        gboolean newer = md_version_newer(MD_APP_VERSION, updates->app->version);
        g_autofree gchar *text =
            newer
                ? g_strdup_printf("Aplicación " MD_APP_VERSION " · Nueva versión: %s",
                                  updates->app->version)
                : g_strdup("Aplicación " MD_APP_VERSION " · Estás usando la versión más reciente.");
        gtk_label_set_text(GTK_LABEL(w->app_update_label), text);
        gtk_widget_set_visible(w->app_update_button, newer);
    }
    refresh_update_controls(w);
}
static void check_updates(Window *w) {
    if (w->closing || w->checking_updates || w->updating)
        return;
    w->checking_updates = TRUE;
    w->pending++;
    g_cancellable_reset(w->updates_cancel);
    gtk_label_set_text(GTK_LABEL(w->engine_update_label), "Comprobando actualizaciones de yt-dlp…");
    gtk_label_set_text(GTK_LABEL(w->app_update_label), "Comprobando versiones de la aplicación…");
    gtk_widget_set_visible(w->engine_update_button, FALSE);
    gtk_widget_set_visible(w->app_update_button, FALSE);
    refresh_update_controls(w);
    md_updates_check_async(w->updates_cancel, updates_checked, w);
}
static void check_updates_clicked(GtkButton *button, gpointer data) {
    (void)button;
    check_updates(data);
}
static void engine_updated(GObject *source, GAsyncResult *result, gpointer data) {
    (void)source;
    Window *w = data;
    w->pending--;
    w->updating = FALSE;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *version = md_engine_update_finish(result, &error);
    if (w->closing)
        return;
    if (!version) {
        gtk_widget_set_sensitive(w->analyze, w->dependencies);
        update_selection(w);
        message(w, error ? error->message : "No se pudo actualizar yt-dlp.");
        refresh_update_controls(w);
        gtk_button_set_label(GTK_BUTTON(w->engine_update_button), "Reintentar actualización");
        return;
    }
    g_autofree gchar *text = g_strdup_printf("yt-dlp %s actualizado correctamente.", version);
    message(w, text);
    clear_video(w);
    update_selection(w);
    w->pending++;
    md_dependencies_async(dependencies_done, w);
    check_updates(w);
}
static void engine_update_clicked(GtkButton *button, gpointer data) {
    (void)button;
    Window *w = data;
    if (!w->updates || !w->updates->engine || w->updating || w->checking_updates || w->closing)
        return;
    if (w->pending || w->manager->active) {
        message(w, "Espera a que finalicen los análisis y las descargas para actualizar yt-dlp.");
        return;
    }
    w->updating = TRUE;
    w->pending++;
    gtk_widget_set_sensitive(w->analyze, FALSE);
    update_selection(w);
    refresh_update_controls(w);
    gtk_button_set_label(GTK_BUTTON(w->engine_update_button), "Actualizando…");
    message(w, "Descargando y verificando la nueva versión de yt-dlp…");
    md_engine_update_async(w->updates->engine, w->updates_cancel, engine_updated, w);
}
static void app_update_clicked(GtkButton *button, gpointer data) {
    (void)button;
    Window *w = data;
    if (w->updates && w->updates->app)
        gtk_show_uri(GTK_WINDOW(w->window), w->updates->app->page_url, GDK_CURRENT_TIME);
}

void md_window_present(GtkApplication *application) {
    GtkWindow *existing = gtk_application_get_active_window(application);
    if (existing) {
        gtk_window_present(existing);
        return;
    }
    Window *w = g_new0(Window, 1);
    w->app = application;
    w->prefs = md_preferences_load();
    w->manager = md_manager_new(job_changed, w);
    w->manager->limit = w->prefs->concurrency;
    w->analysis_cancel = g_cancellable_new();
    w->thumbnail_cancel = g_cancellable_new();
    w->updates_cancel = g_cancellable_new();
    w->heights = g_array_new(FALSE, FALSE, sizeof(gint));
    w->checks = g_ptr_array_new();
    w->window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(w->window), "Media Downloader");
    gtk_window_set_default_size(GTK_WINDOW(w->window), 920, 940);
    gtk_widget_set_size_request(w->window, 620, 540);
    apply_theme(w);
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(
        css, "/io/github/fernandorojas/MediaDownloader/styles/style.css");
    gtk_window_set_icon_name(GTK_WINDOW(w->window), "io.github.fernandorojas.MediaDownloader");
    gtk_icon_theme_add_resource_path(
        gtk_icon_theme_get_for_display(gtk_widget_get_display(w->window)),
        "/io/github/fernandorojas/MediaDownloader/icons");
    gtk_style_context_add_provider_for_display(gtk_widget_get_display(w->window),
                                               GTK_STYLE_PROVIDER(css),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    GtkWidget *header = gtk_header_bar_new();
    GtkWidget *headtitle = label("Media Downloader", "heading");
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), headtitle);
    GtkWidget *prefs = icon_button("emblem-system-symbolic", "Preferencias"),
              *theme = icon_button("weather-clear-night-symbolic", "Alternar tema claro y oscuro"),
              *about = icon_button("help-about-symbolic", "Acerca de");
    w->check_updates_button = icon_button("view-refresh-symbolic", "Buscar actualizaciones");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), w->check_updates_button);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), about);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), prefs);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), theme);
    gtk_window_set_titlebar(GTK_WINDOW(w->window), header);
    GtkWidget *scroll = gtk_scrolled_window_new(), *content = box(GTK_ORIENTATION_VERTICAL, 20);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    margins(content, 28);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), content);
    gtk_window_set_child(GTK_WINDOW(w->window), scroll);
    gtk_box_append(GTK_BOX(content), label("Interfaz gráfica para yt-dlp", "title"));
    gtk_box_append(GTK_BOX(content),
                   label("Video y audio, directamente en tu equipo.", "dim-label"));
    GtkWidget *updates_box = box(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *engine_line = box(GTK_ORIENTATION_HORIZONTAL, 12),
              *app_line = box(GTK_ORIENTATION_HORIZONTAL, 12);
    w->engine_update_label = label("Comprobando actualizaciones de yt-dlp…", "dim-label");
    w->app_update_label = label("Comprobando versiones de la aplicación…", "dim-label");
    gtk_widget_set_hexpand(w->engine_update_label, TRUE);
    gtk_widget_set_hexpand(w->app_update_label, TRUE);
    w->engine_update_button = gtk_button_new_with_label("Actualizar yt-dlp");
    gtk_widget_set_tooltip_text(
        w->engine_update_button,
        "Instalar una copia actualizada para este usuario, sin permisos de administrador");
    w->app_update_button = gtk_button_new_with_label("Descargar nueva versión");
    gtk_widget_set_visible(w->engine_update_button, FALSE);
    gtk_widget_set_visible(w->app_update_button, FALSE);
    gtk_box_append(GTK_BOX(engine_line), w->engine_update_label);
    gtk_box_append(GTK_BOX(engine_line), w->engine_update_button);
    gtk_box_append(GTK_BOX(app_line), w->app_update_label);
    gtk_box_append(GTK_BOX(app_line), w->app_update_button);
    gtk_box_append(GTK_BOX(updates_box), engine_line);
    gtk_box_append(GTK_BOX(updates_box), app_line);
    gtk_box_append(GTK_BOX(content), updates_box);
    GtkWidget *analysis = box(GTK_ORIENTATION_HORIZONTAL, 10);
    w->url = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(w->url),
                                   "Pega una URL de video, audio o lista de reproducción");
    gtk_widget_set_hexpand(w->url, TRUE);
    gtk_entry_set_max_length(GTK_ENTRY(w->url), 8192);
    GtkWidget *paste = icon_button("edit-paste-symbolic", "Pegar URL");
    w->analyze = gtk_button_new_with_label("Analizar");
    gtk_widget_add_css_class(w->analyze, "suggested-action");
    gtk_widget_set_sensitive(w->analyze, FALSE);
    w->spinner = gtk_spinner_new();
    gtk_box_append(GTK_BOX(analysis), w->url);
    gtk_box_append(GTK_BOX(analysis), paste);
    gtk_box_append(GTK_BOX(analysis), w->analyze);
    gtk_box_append(GTK_BOX(analysis), w->spinner);
    gtk_box_append(GTK_BOX(content), analysis);
    w->message = label("Comprobando yt-dlp y FFmpeg…", "dim-label");
    gtk_label_set_selectable(GTK_LABEL(w->message), TRUE);
    gtk_box_append(GTK_BOX(content), w->message);
    GtkWidget *card = box(GTK_ORIENTATION_VERTICAL, 16);
    margins(card, 18);
    gtk_widget_add_css_class(card, "card");
    GtkWidget *metadata = box(GTK_ORIENTATION_HORIZONTAL, 18);
    w->picture = gtk_picture_new();
    gtk_widget_set_size_request(w->picture, 180, 104);
    gtk_picture_set_can_shrink(GTK_PICTURE(w->picture), TRUE);
    GtkWidget *text = box(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_hexpand(text, TRUE);
    w->title = label("Tu próxima descarga", "heading");
    w->meta = label("Título, autor, duración y formatos aparecerán aquí.", "dim-label");
    gtk_box_append(GTK_BOX(text), w->title);
    gtk_box_append(GTK_BOX(text), w->meta);
    GtkWidget *thumbnail = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(thumbnail), w->picture);
    w->placeholder = gtk_image_new_from_icon_name("video-x-generic-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(w->placeholder), 48);
    gtk_widget_add_css_class(w->placeholder, "dim-label");
    gtk_overlay_add_overlay(GTK_OVERLAY(thumbnail), w->placeholder);
    gtk_box_append(GTK_BOX(metadata), thumbnail);
    gtk_box_append(GTK_BOX(metadata), text);
    gtk_box_append(GTK_BOX(card), metadata);
    GtkWidget *selectors = box(GTK_ORIENTATION_HORIZONTAL, 14);
    w->format = gtk_drop_down_new_from_strings(container_labels);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(w->format), container_index(w->prefs->video));
    const gchar *qualities[] = {"Mejor calidad disponible", NULL};
    w->quality = gtk_drop_down_new_from_strings(qualities);
    gtk_widget_set_hexpand(w->format, TRUE);
    gtk_widget_set_hexpand(w->quality, TRUE);
    gtk_box_append(GTK_BOX(selectors), w->format);
    gtk_box_append(GTK_BOX(selectors), w->quality);
    GtkWidget *preferred_audio =
        icon_button("audio-x-generic-symbolic", "Seleccionar formato de audio preferido");
    g_signal_connect(preferred_audio, "clicked", G_CALLBACK(preferred_audio_clicked), w);
    gtk_box_append(GTK_BOX(selectors), preferred_audio);
    gtk_box_append(GTK_BOX(card), selectors);
    w->hint = label("", "dim-label");
    gtk_box_append(GTK_BOX(card), w->hint);
    GtkWidget *destination = box(GTK_ORIENTATION_HORIZONTAL, 12);
    w->destination = gtk_button_new_with_label(w->prefs->directory);
    gtk_widget_set_hexpand(w->destination, TRUE);
    gtk_widget_set_tooltip_text(w->destination, "Seleccionar carpeta de destino");
    w->download = gtk_button_new_with_label("Descargar");
    gtk_widget_add_css_class(w->download, "suggested-action");
    gtk_box_append(GTK_BOX(destination), w->destination);
    gtk_box_append(GTK_BOX(destination), w->download);
    gtk_box_append(GTK_BOX(card), destination);
    w->playlist = box(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(card), w->playlist);
    gtk_box_append(GTK_BOX(content), card);
    GtkWidget *listhead = box(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *listtitle = label("Tus descargas", "heading");
    gtk_widget_set_hexpand(listtitle, TRUE);
    w->counter = label("0 elementos · 0 activos", "dim-label");
    gtk_box_append(GTK_BOX(listhead), listtitle);
    gtk_box_append(GTK_BOX(listhead), w->counter);
    gtk_box_append(GTK_BOX(content), listhead);
    w->list = box(GTK_ORIENTATION_VERTICAL, 12);
    gtk_box_append(GTK_BOX(content), w->list);
    gtk_box_append(
        GTK_BOX(content),
        label("Descarga solo contenido propio, de dominio público o autorizado. Respeta los "
              "derechos de autor, los términos del sitio y la legislación aplicable.",
              "dim-label"));
    g_signal_connect(w->engine_update_button, "clicked", G_CALLBACK(engine_update_clicked), w);
    g_signal_connect(w->app_update_button, "clicked", G_CALLBACK(app_update_clicked), w);
    g_signal_connect(w->check_updates_button, "clicked", G_CALLBACK(check_updates_clicked), w);
    g_signal_connect(prefs, "clicked", G_CALLBACK(preferences_clicked), w);
    g_signal_connect(theme, "clicked", G_CALLBACK(theme_clicked), w);
    g_signal_connect(about, "clicked", G_CALLBACK(about_clicked), w);
    g_signal_connect(paste, "clicked", G_CALLBACK(paste_clicked), w);
    g_signal_connect(w->analyze, "clicked", G_CALLBACK(analyze_clicked), w);
    g_signal_connect(w->url, "changed", G_CALLBACK(url_changed), w);
    g_signal_connect(w->format, "notify::selected", G_CALLBACK(selection_changed), w);
    g_signal_connect(w->quality, "notify::selected", G_CALLBACK(selection_changed), w);
    g_signal_connect(w->destination, "clicked", G_CALLBACK(choose_folder), w);
    g_signal_connect(w->download, "clicked", G_CALLBACK(download_clicked), w);
    g_signal_connect(w->window, "close-request", G_CALLBACK(close_requested), w);
    update_selection(w);
    gtk_window_present(GTK_WINDOW(w->window));
    w->pending++;
    md_dependencies_async(dependencies_done, w);
    check_updates(w);
}
