#include "video-info.h"
#include "utils.h"
#include <math.h>
const gchar *md_json_string(JsonObject *obj, const gchar *key) {
    JsonNode *n = json_object_get_member(obj, key);
    return n && JSON_NODE_HOLDS_VALUE(n) && json_node_get_value_type(n) == G_TYPE_STRING
               ? json_node_get_string(n)
               : NULL;
}
gdouble md_json_number(JsonObject *obj, const gchar *key, gdouble fallback) {
    JsonNode *n = json_object_get_member(obj, key);
    if (!n || !JSON_NODE_HOLDS_VALUE(n))
        return fallback;
    GType type = json_node_get_value_type(n);
    if (type != G_TYPE_INT64 && type != G_TYPE_DOUBLE)
        return fallback;
    gdouble value = json_node_get_double(n);
    return isfinite(value) ? value : fallback;
}
static void format_free(gpointer data) {
    MdFormat *f = data;
    g_free(f->id);
    g_free(f->ext);
    g_free(f->vcodec);
    g_free(f->acodec);
    g_free(f->language);
    g_free(f);
}
void md_video_free(MdVideo *v) {
    if (!v)
        return;
    g_free(v->url);
    g_free(v->title);
    g_free(v->author);
    g_free(v->site);
    g_free(v->thumbnail);
    g_ptr_array_unref(v->formats);
    g_ptr_array_unref(v->entries);
    g_free(v);
}
MdVideo *md_video_from_object(JsonObject *obj, GError **error) {
    const gchar *title = md_json_string(obj, "title");
    if (!title || !*title) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "El contenido no tiene un título válido.");
        return NULL;
    }
    MdVideo *v = g_new0(MdVideo, 1);
    v->title = g_strdup(title);
    v->url = g_strdup(md_json_string(obj, "webpage_url"));
    if (!md_url_valid(v->url)) {
        g_free(v->url);
        v->url = g_strdup(md_json_string(obj, "url"));
    }
    v->author = g_strdup(md_json_string(obj, "uploader"));
    v->site = g_strdup(md_json_string(obj, "extractor_key"));
    v->thumbnail = g_strdup(md_json_string(obj, "thumbnail"));
    v->duration = md_json_number(obj, "duration", -1);
    v->formats = g_ptr_array_new_with_free_func(format_free);
    v->entries = g_ptr_array_new_with_free_func((GDestroyNotify)md_video_free);
    JsonNode *node = json_object_get_member(obj, "formats");
    if (node && JSON_NODE_HOLDS_ARRAY(node)) {
        JsonArray *a = json_node_get_array(node);
        for (guint i = 0; i < json_array_get_length(a); i++) {
            JsonNode *item = json_array_get_element(a, i);
            if (!JSON_NODE_HOLDS_OBJECT(item))
                continue;
            JsonObject *o = json_node_get_object(item);
            const gchar *id = md_json_string(o, "format_id");
            if (!id || !*id ||
                strspn(id, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.:") !=
                    strlen(id))
                continue;
            MdFormat *f = g_new0(MdFormat, 1);
            f->id = g_strdup(id);
            f->ext = g_strdup(md_json_string(o, "ext"));
            f->vcodec = g_strdup(md_json_string(o, "vcodec"));
            f->acodec = g_strdup(md_json_string(o, "acodec"));
            gdouble height = md_json_number(o, "height", 0);
            f->height = height > 0 && height < 20000 ? (gint)height : 0;
            f->bitrate = md_json_number(o, "tbr", 0);
            f->language = g_strdup(md_json_string(o, "language"));
            f->language_preference = md_json_number(o, "language_preference", -1);
            JsonNode *drm = json_object_get_member(o, "has_drm");
            f->drm = drm && JSON_NODE_HOLDS_VALUE(drm) &&
                     json_node_get_value_type(drm) == G_TYPE_BOOLEAN && json_node_get_boolean(drm);
            g_ptr_array_add(v->formats, f);
        }
    }
    node = json_object_get_member(obj, "entries");
    if (node && JSON_NODE_HOLDS_ARRAY(node)) {
        JsonArray *a = json_node_get_array(node);
        for (guint i = 0; i < MIN(json_array_get_length(a), 100u); i++) {
            JsonNode *item = json_array_get_element(a, i);
            if (!JSON_NODE_HOLDS_OBJECT(item))
                continue;
            MdVideo *entry = md_video_from_object(json_node_get_object(item), NULL);
            if (entry && g_strcmp0(entry->url, v->url) == 0) {
                const gchar *direct = md_json_string(json_node_get_object(item), "url");
                if (md_url_valid(direct)) {
                    g_free(entry->url);
                    entry->url = g_strdup(direct);
                }
            }
            if (entry && md_url_valid(entry->url))
                g_ptr_array_add(v->entries, entry);
            else
                md_video_free(entry);
        }
    }
    return v;
}
MdVideo *md_video_parse(const gchar *data, GError **error) {
    g_autoptr(JsonParser) parser = json_parser_new();
    if (!json_parser_load_from_data(parser, data, -1, error))
        return NULL;
    JsonNode *root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "Se esperaba un objeto JSON.");
        return NULL;
    }
    return md_video_from_object(json_node_get_object(root), error);
}
