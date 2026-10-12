#include "format-selector.h"
static gboolean codec(const gchar *s) {
    return !s || (*s && !g_str_equal(s, "none"));
}
static gboolean prefix(const gchar *s, const gchar *p) {
    return s && g_str_has_prefix(s, p);
}
static gboolean compatible(MdFormat *f, const gchar *container) {
    if (!f->vcodec || !f->acodec)
        return g_strcmp0(f->ext, container) == 0;
    if (g_str_equal(container, "mkv"))
        return TRUE;
    if (g_str_equal(container, "mp4"))
        return (!codec(f->vcodec) || prefix(f->vcodec, "avc") || prefix(f->vcodec, "h264") ||
                prefix(f->vcodec, "hev") || prefix(f->vcodec, "hvc") ||
                prefix(f->vcodec, "av01")) &&
               (!codec(f->acodec) || prefix(f->acodec, "mp4a") || prefix(f->acodec, "aac"));
    return (!codec(f->vcodec) || prefix(f->vcodec, "vp8") || prefix(f->vcodec, "vp9") ||
            prefix(f->vcodec, "vp0") || prefix(f->vcodec, "av01")) &&
           (!codec(f->acodec) || prefix(f->acodec, "opus") || prefix(f->acodec, "vorbis"));
}
const gchar *md_processing_name(MdProcessing p) {
    return p == MD_DIRECT  ? "Formato directo"
           : p == MD_MERGE ? "Requiere combinar pistas o remultiplexar"
                           : "Requiere conversión con FFmpeg";
}
void md_selection_free(MdSelection *s) {
    if (s) {
        g_free(s->selector);
        g_free(s->container);
        g_free(s);
    }
}
GArray *md_resolutions(MdVideo *v) {
    GArray *a = g_array_new(FALSE, FALSE, sizeof(gint));
    for (guint i = 0; i < v->formats->len; i++) {
        MdFormat *f = g_ptr_array_index(v->formats, i);
        if (f->drm || !codec(f->vcodec) || f->height <= 0)
            continue;
        gboolean exists = FALSE;
        for (guint j = 0; j < a->len; j++)
            if (g_array_index(a, gint, j) == f->height)
                exists = TRUE;
        if (!exists)
            g_array_append_val(a, f->height);
    }
    return a;
}
MdSelection *md_select_format(MdVideo *v, const gchar *container, gint height, GError **error) {
    gboolean audio = g_str_equal(container, "mp3") || g_str_equal(container, "m4a") ||
                     g_str_equal(container, "opus") || g_str_equal(container, "flac") ||
                     g_str_equal(container, "wav");
    if (!audio && !g_str_equal(container, "mp4") && !g_str_equal(container, "webm") &&
        !g_str_equal(container, "mkv"))
        goto unavailable;
    MdFormat *video = NULL, *sound = NULL;
    for (guint i = 0; i < v->formats->len; i++) {
        MdFormat *f = g_ptr_array_index(v->formats, i);
        if (f->drm)
            continue;
        if (audio) {
            if (codec(f->acodec) &&
                (!sound || (!codec(f->vcodec) && codec(sound->vcodec)) ||
                 (codec(f->vcodec) == codec(sound->vcodec) && f->bitrate > sound->bitrate)))
                sound = f;
        } else if (compatible(f, container)) {
            if (codec(f->vcodec) && (!height || f->height == height) &&
                (!video || f->height > video->height ||
                 (f->height == video->height && f->bitrate > video->bitrate)))
                video = f;
            if (!codec(f->vcodec) && codec(f->acodec) && (!sound || f->bitrate > sound->bitrate))
                sound = f;
        }
    }
    /* A video-only candidate is usable only when a compatible audio track exists.
     * Fall back to a combined format at the requested resolution when it does not. */
    if (!audio && video && !codec(video->acodec) && !sound) {
        video = NULL;
        for (guint i = 0; i < v->formats->len; i++) {
            MdFormat *f = g_ptr_array_index(v->formats, i);
            if (!f->drm && compatible(f, container) && codec(f->vcodec) && codec(f->acodec) &&
                (!height || f->height == height) &&
                (!video || f->height > video->height ||
                 (f->height == video->height && f->bitrate > video->bitrate)))
                video = f;
        }
    }
    if ((audio && !sound) || (!audio && (!video || (!codec(video->acodec) && !sound))))
        goto unavailable;
    MdSelection *s = g_new0(MdSelection, 1);
    s->audio = audio;
    s->container = g_strdup(container);
    if (audio) {
        s->selector = g_strdup(sound->id);
        s->processing =
            !codec(sound->vcodec) && g_strcmp0(sound->ext, container) == 0 ? MD_DIRECT : MD_CONVERT;
    } else if (codec(video->acodec)) {
        s->selector = g_strdup(video->id);
        s->processing = g_strcmp0(video->ext, container) == 0 ? MD_DIRECT : MD_MERGE;
    } else {
        s->selector = g_strdup_printf("%s+%s", video->id, sound->id);
        s->processing = MD_MERGE;
    }
    return s;
unavailable:
    g_set_error_literal(
        error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
        "No hay pistas compatibles con ese formato y resolución. Prueba MKV u otra calidad.");
    return NULL;
}
