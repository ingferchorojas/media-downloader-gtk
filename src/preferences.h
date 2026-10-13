#pragma once
#include <gio/gio.h>
typedef struct {
    gchar *directory, *video, *audio, *theme;
    gint quality;
    guint concurrency, retries;
    gboolean number_playlist;
    gboolean notify, open_folder;
} MdPreferences;
MdPreferences *md_preferences_load(void);
gboolean md_preferences_save(MdPreferences *preferences, GError **error);
void md_preferences_free(MdPreferences *preferences);
