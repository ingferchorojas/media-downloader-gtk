#include "format-selector.h"
static gboolean codec(const gchar *s) {
    return !s || (*s && !g_str_equal(s, "none"));
}
static gboolean better_audio(MdFormat *candidate, MdFormat *current) {
    if (!current)
        return TRUE;
    /* Respect the extractor's original/default language before ranking quality. */
    if (candidate->language_preference != current->language_preference)
        return candidate->language_preference > current->language_preference;
    if (codec(candidate->vcodec) != codec(current->vcodec))
        return !codec(candidate->vcodec);
    return candidate->bitrate > current->bitrate;
}
static gboolean better_video(MdFormat *candidate, MdFormat *current) {
    if (!current || candidate->height != current->height)
        return !current || candidate->height > current->height;
    if (codec(candidate->acodec) && codec(current->acodec) &&
        candidate->language_preference != current->language_preference)
        return candidate->language_preference > current->language_preference;
    return candidate->bitrate > current->bitrate;
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
        g_free(s->audio_language);
        for (guint i = 0; i < s->tracks; i++)
            g_free(s->track_ids[i]);
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
    gboolean has_audio = FALSE;
    for (guint i = 0; i < v->formats->len; i++) {
        MdFormat *f = g_ptr_array_index(v->formats, i);
        if (!f->drm && codec(f->acodec))
            has_audio = TRUE;
    }
    for (guint i = 0; i < v->formats->len; i++) {
        MdFormat *f = g_ptr_array_index(v->formats, i);
        if (f->drm)
            continue;
        if (audio) {
            if (codec(f->acodec) && better_audio(f, sound))
                sound = f;
        } else if (compatible(f, container)) {
            if (codec(f->vcodec) && (!height || f->height == height) && better_video(f, video))
                video = f;
            if (!codec(f->vcodec) && codec(f->acodec) && better_audio(f, sound))
                sound = f;
        }
    }
    /* A video-only candidate is usable only when a compatible audio track exists.
     * Fall back to a combined format at the requested resolution when it does not. */
    if (!audio && has_audio && video && !codec(video->acodec) && !sound) {
        video = NULL;
        for (guint i = 0; i < v->formats->len; i++) {
            MdFormat *f = g_ptr_array_index(v->formats, i);
            if (!f->drm && compatible(f, container) && codec(f->vcodec) && codec(f->acodec) &&
                (!height || f->height == height) && better_video(f, video))
                video = f;
        }
    }
    if ((audio && !sound) || (!audio && (!video || (has_audio && !codec(video->acodec) && !sound))))
        goto unavailable;
    MdSelection *s = g_new0(MdSelection, 1);
    s->audio = audio;
    s->container = g_strdup(container);
    if (audio) {
        s->selector = g_strdup(sound->id);
        s->processing =
            !codec(sound->vcodec) && g_strcmp0(sound->ext, container) == 0 ? MD_DIRECT : MD_CONVERT;
    } else if (codec(video->acodec) || !has_audio) {
        s->selector = g_strdup(video->id);
        s->processing = g_strcmp0(video->ext, container) == 0 ? MD_DIRECT : MD_MERGE;
    } else {
        s->selector = g_strdup_printf("%s+%s", video->id, sound->id);
        s->processing = MD_MERGE;
    }
    MdFormat *selected[] = {audio ? sound : video, sound};
    s->tracks = !audio && !codec(video->acodec) && has_audio ? 2 : 1;
    for (guint i = 0; i < s->tracks; i++) {
        s->track_ids[i] = g_strdup(selected[i]->id);
        s->track_sizes[i] = selected[i]->size;
        s->track_estimated[i] = selected[i]->size_estimated;
    }
    MdFormat *audio_format = audio || !codec(video->acodec) ? sound : video;
    s->audio_language = audio_format ? g_strdup(audio_format->language) : NULL;
    return s;
unavailable:
    g_set_error_literal(
        error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
        "No hay pistas compatibles con ese formato y resolución. Prueba MKV u otra calidad.");
    return NULL;
}
