#include "preferences.h"
#include <glib/gstdio.h>
static gchar *config_path(void) {
    return g_build_filename(g_get_user_config_dir(), "media-downloader-gtk", "preferences.ini",
                            NULL);
}
static gchar *get_string(GKeyFile *k, const gchar *key, const gchar *fallback) {
    gchar *s = g_key_file_get_string(k, "Preferences", key, NULL);
    return s && *s ? s : (g_free(s), g_strdup(fallback));
}
MdPreferences *md_preferences_load(void) {
    g_autoptr(GKeyFile) k = g_key_file_new();
    g_autofree gchar *path = config_path();
    g_key_file_load_from_file(k, path, G_KEY_FILE_NONE, NULL);
    MdPreferences *p = g_new0(MdPreferences, 1);
    const gchar *dir = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    if (!dir)
        dir = g_get_home_dir();
    p->directory = get_string(k, "directory", dir);
    p->video = get_string(k, "video", "mp4");
    p->audio = get_string(k, "audio", "mp3");
    p->theme = get_string(k, "theme", "system");
    p->quality = MAX(0, g_key_file_get_integer(k, "Preferences", "quality", NULL));
    p->concurrency =
        (guint)CLAMP(g_key_file_get_integer(k, "Preferences", "concurrency", NULL), 1, 8);
    if (!g_key_file_has_key(k, "Preferences", "concurrency", NULL))
        p->concurrency = 2;
    p->notify = !g_key_file_has_key(k, "Preferences", "notify", NULL) ||
                g_key_file_get_boolean(k, "Preferences", "notify", NULL);
    p->open_folder = g_key_file_get_boolean(k, "Preferences", "open_folder", NULL);
    return p;
}
gboolean md_preferences_save(MdPreferences *p, GError **error) {
    g_autoptr(GKeyFile) k = g_key_file_new();
    g_autofree gchar *path = config_path();
    g_autofree gchar *dir = g_path_get_dirname(path);
    if (g_mkdir_with_parents(dir, 0700) < 0) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "No se pudo crear la carpeta de preferencias.");
        return FALSE;
    }
    g_key_file_set_string(k, "Preferences", "directory", p->directory);
    g_key_file_set_string(k, "Preferences", "video", p->video);
    g_key_file_set_string(k, "Preferences", "audio", p->audio);
    g_key_file_set_string(k, "Preferences", "theme", p->theme);
    g_key_file_set_integer(k, "Preferences", "quality", p->quality);
    g_key_file_set_integer(k, "Preferences", "concurrency", (gint)p->concurrency);
    g_key_file_set_boolean(k, "Preferences", "notify", p->notify);
    g_key_file_set_boolean(k, "Preferences", "open_folder", p->open_folder);
    gsize len;
    g_autofree gchar *contents = g_key_file_to_data(k, &len, NULL);
    return g_file_set_contents_full(path, contents, (gssize)len,
                                    G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE,
                                    0600, error);
}
void md_preferences_free(MdPreferences *p) {
    if (p) {
        g_free(p->directory);
        g_free(p->video);
        g_free(p->audio);
        g_free(p->theme);
        g_free(p);
    }
}
