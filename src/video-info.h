#pragma once
#include <json-glib/json-glib.h>
typedef struct {
    gchar *id, *ext, *vcodec, *acodec, *language;
    gint height;
    gdouble bitrate, language_preference;
    gboolean drm;
} MdFormat;
typedef struct _MdVideo {
    gchar *url, *title, *author, *site, *thumbnail;
    gdouble duration;
    GPtrArray *formats, *entries;
} MdVideo;
MdVideo *md_video_parse(const gchar *data, GError **error);
MdVideo *md_video_from_object(JsonObject *obj, GError **error);
void md_video_free(MdVideo *video);
const gchar *md_json_string(JsonObject *obj, const gchar *key);
gdouble md_json_number(JsonObject *obj, const gchar *key, gdouble fallback);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(MdVideo, md_video_free)
