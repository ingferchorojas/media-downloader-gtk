#pragma once
#include <gio/gio.h>
#define MD_APP_REPOSITORY "ingferchorojas/media-downloader-gtk"
#define MD_ENGINE_REPOSITORY "yt-dlp/yt-dlp"
typedef struct {
    gchar *version, *page_url, *download_url, *checksum_url, *digest;
} MdRelease;
typedef struct {
    gchar *installed_engine, *engine_error, *app_error;
    MdRelease *engine, *app;
    gboolean app_unpublished;
} MdUpdates;
void md_release_free(MdRelease *release);
void md_updates_free(MdUpdates *updates);
MdRelease *md_release_parse(const gchar *json, const gchar *repository, gboolean engine,
                            GError **error);
gboolean md_version_newer(const gchar *installed, const gchar *available);
gboolean md_checksum_matches(GBytes *bytes, const gchar *sha256);
void md_updates_check_async(GCancellable *cancel, GAsyncReadyCallback callback, gpointer data);
MdUpdates *md_updates_check_finish(GAsyncResult *result, GError **error);
/* Blocking validation and atomic installation; callers run this off the GTK thread. */
gchar *md_engine_install(GBytes *bytes, const gchar *sha256, const gchar *version,
                         GCancellable *cancel, GError **error);
void md_engine_update_async(const MdRelease *release, GCancellable *cancel,
                            GAsyncReadyCallback callback, gpointer data);
gchar *md_engine_update_finish(GAsyncResult *result, GError **error);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(MdRelease, md_release_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(MdUpdates, md_updates_free)
