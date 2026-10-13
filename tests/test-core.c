#include <glib.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <glib/gstdio.h>
#include "utils.h"
#include "download-manager.h"
#include "preferences.h"
#include "updates.h"
#include "engine.h"
static void monotonic_progress(void) {
    MdManager *m = md_manager_new(NULL, NULL);
    MdJob *j = md_manager_add(m, "https://example.com", "test", "/tmp", "mp4", 0);
    j->expected_tracks = 2;
    MdProgress p = {.filename = "video", .downloaded = 80, .total = 100};
    md_job_apply_progress(j, &p);
    g_assert_cmpfloat_with_epsilon(j->fraction, 0.4, 0.00001);
    p.total = 200;
    md_job_apply_progress(j, &p);
    g_assert_cmpfloat_with_epsilon(j->fraction, 0.4, 0.00001);
    p.status = "finished";
    md_job_apply_progress(j, &p);
    g_assert_cmpfloat_with_epsilon(j->fraction, 0.5, 0.00001);
    p.filename = "audio";
    p.status = "downloading";
    p.downloaded = 10;
    p.total = 100;
    md_job_apply_progress(j, &p);
    gdouble previous = j->fraction;
    p.downloaded = 0;
    md_job_apply_progress(j, &p);
    g_assert_cmpfloat(j->fraction, ==, previous);
    p.total = -1;
    p.downloaded = -1;
    md_job_apply_progress(j, &p);
    g_assert_cmpfloat(j->fraction, ==, previous);
    p.status = "finished";
    md_job_apply_progress(j, &p);
    g_assert_cmpfloat_with_epsilon(j->fraction, 0.99, 0.00001);
    p.stage = "Merger";
    md_job_apply_progress(j, &p);
    g_assert_cmpfloat(j->fraction, <, 1);
    j->state = MD_CANCELLED;
    m->stopping = TRUE;
    md_job_retry(j);
    g_assert_cmpfloat(j->fraction, ==, -1);
    g_assert_cmpuint(g_hash_table_size(j->track_progress), ==, 0);
    md_manager_free(m);
}
static void fragment_progress(void) {
    MdManager *m = md_manager_new(NULL, NULL);
    MdJob *j = md_manager_add(m, "https://example.com", "test", "/tmp", "mp4", 0);
    j->expected_tracks = 2;
    /* Actual first event from YouTube video 7mtza4rqxTo. */
    MdProgress p;
    g_assert_true(md_progress_parse(
        "MD_PROGRESS:{\"status\":\"downloading\",\"downloaded_bytes\":712,"
        "\"total_bytes_estimate\":712,\"fragment_index\":0,\"fragment_count\":255,"
        "\"filename\":\"video.f270.mp4\"}",
        &p));
    md_job_apply_progress(j, &p);
    g_assert_cmpfloat(j->fraction, ==, 0);
    p.fragment = 1;
    p.total = 363120;
    md_job_apply_progress(j, &p);
    g_assert_cmpfloat_with_epsilon(j->fraction, 0.5 / 255, 0.000001);
    p.downloaded = 1736;
    p.total = 90780;
    md_job_apply_progress(j, &p);
    g_assert_cmpfloat_with_epsilon(j->fraction, 0.5 / 255, 0.000001);
    p.fragment = 128;
    md_job_apply_progress(j, &p);
    g_assert_cmpfloat_with_epsilon(j->fraction, 0.5 * 128 / 255, 0.000001);
    p.fragment = 127;
    md_job_apply_progress(j, &p);
    g_assert_cmpfloat_with_epsilon(j->fraction, 0.5 * 128 / 255, 0.000001);
    g_free(p.status);
    p.status = g_strdup("finished");
    md_job_apply_progress(j, &p);
    g_assert_cmpfloat_with_epsilon(j->fraction, 0.5, 0.000001);
    md_progress_clear(&p);
    md_manager_free(m);
}
static void weighted_progress(void) {
    g_autoptr(MdVideo) v = md_video_parse(
        "{\"title\":\"Weighted\",\"duration\":10,\"formats\":["
        "{\"format_id\":\"v\",\"ext\":\"mp4\",\"height\":1080,\"vcodec\":\"avc1\","
        "\"acodec\":\"none\",\"filesize\":900},"
        "{\"format_id\":\"a\",\"ext\":\"m4a\",\"vcodec\":\"none\",\"acodec\":\"mp4a\","
        "\"filesize_approx\":100}]}", NULL);
    g_autoptr(MdSelection) s = md_select_format(v, "mp4", 1080, NULL);
    MdManager *m = md_manager_new(NULL, NULL);
    MdJob *j = md_manager_add(m, "https://example.com", "test", "/tmp", "mp4", 0);
    md_job_prepare_tracks(j, s);
    g_assert_cmpfloat(j->total_bytes, ==, 1000);
    g_assert_true(j->total_estimated);
    MdProgress p;
    g_assert_true(md_progress_parse(
        "MD_PROGRESS:{\"progress\":{\"status\":\"downloading\",\"downloaded_bytes\":450,"
        "\"total_bytes\":900,\"filename\":\"video.mp4\"},\"format_id\":\"v\"}", &p));
    md_job_apply_progress(j, &p);
    g_assert_cmpfloat_with_epsilon(j->fraction, 0.45, 0.000001);
    g_assert_cmpfloat(j->downloaded_bytes, ==, 450);
    p.downloaded = 900;
    g_free(p.status);
    p.status = g_strdup("finished");
    md_job_apply_progress(j, &p);
    g_assert_cmpfloat_with_epsilon(j->fraction, 0.9, 0.000001);
    md_progress_clear(&p);
    p = (MdProgress){.format_id = "a", .downloaded = 50, .total = 100};
    md_job_apply_progress(j, &p);
    g_assert_cmpfloat_with_epsilon(j->fraction, 0.95, 0.000001);
    g_assert_true(j->total_estimated);
    g_assert_cmpfloat(j->downloaded_bytes, ==, 950);
    p.downloaded = 100;
    p.status = "finished";
    md_job_apply_progress(j, &p);
    g_assert_cmpfloat(j->fraction, ==, 0.99);
    g_assert_cmpfloat(j->downloaded_bytes, ==, 1000);
    g_assert_false(j->total_estimated);
    /* An initialization fragment must not replace a metadata estimate. */
    s->track_estimated[0] = TRUE;
    md_job_prepare_tracks(j, s);
    j->fraction = -1;
    p = (MdProgress){.format_id = "v", .downloaded = 1, .total = 1,
                     .estimated = TRUE, .fragments = 100};
    md_job_apply_progress(j, &p);
    g_assert_cmpfloat(j->total_bytes, ==, 1000);
    g_assert_cmpfloat_with_epsilon(j->fraction, 0.001, 0.000001);
    p.fragment = 10;
    p.downloaded = 400;
    p.total = 1800;
    md_job_apply_progress(j, &p);
    g_assert_cmpfloat(j->total_bytes, ==, 1000);
    g_assert_true(j->total_estimated);
    p.fragment = 20;
    p.total = 700;
    md_job_apply_progress(j, &p);
    g_assert_cmpfloat(j->total_bytes, ==, 1000);
    p.downloaded = 1200;
    p.status = "finished";
    md_job_apply_progress(j, &p);
    g_assert_cmpfloat(j->total_bytes, ==, 1000);
    g_assert_true(j->total_estimated);
    g_assert_cmpfloat(j->fraction, <, 1);
    p = (MdProgress){.format_id = "a", .downloaded = 30, .total = 50};
    md_job_apply_progress(j, &p);
    g_assert_cmpfloat(j->total_bytes, ==, 1000);
    g_assert_true(j->total_estimated);
    p.downloaded = 130;
    p.status = "finished";
    md_job_apply_progress(j, &p);
    g_assert_cmpfloat(j->total_bytes, ==, 1330);
    g_assert_false(j->total_estimated);
    /* Do not display a partial size as the total when audio size is missing. */
    s->track_sizes[1] = -1;
    md_job_prepare_tracks(j, s);
    j->fraction = -1;
    p = (MdProgress){.format_id = "v", .downloaded = 450, .total = 900};
    md_job_apply_progress(j, &p);
    g_assert_cmpfloat(j->total_bytes, ==, -1);
    g_assert_cmpfloat_with_epsilon(j->fraction, 0.25, 0.000001);
    md_manager_free(m);
}
static void original_audio(void) {
    g_autoptr(MdVideo) v = md_video_parse(
        "{\"title\":\"Languages\",\"formats\":["
        "{\"format_id\":\"v\",\"ext\":\"mp4\",\"height\":1080,\"vcodec\":\"avc1\",\"acodec\":"
        "\"none\"},"
        "{\"format_id\":\"en\",\"ext\":\"m4a\",\"vcodec\":\"none\",\"acodec\":\"mp4a\","
        "\"language\":\"en-US\",\"language_preference\":-1,\"tbr\":200},"
        "{\"format_id\":\"es-low\",\"ext\":\"m4a\",\"vcodec\":\"none\",\"acodec\":\"mp4a\","
        "\"language\":\"es-US\",\"language_preference\":10,\"tbr\":48},"
        "{\"format_id\":\"es\",\"ext\":\"m4a\",\"vcodec\":\"none\",\"acodec\":\"mp4a\","
        "\"language\":\"es-US\",\"language_preference\":10,\"tbr\":128}]}",
        NULL);
    g_assert_nonnull(v);
    MdFormat *f = g_ptr_array_index(v->formats, 3);
    g_assert_cmpstr(f->language, ==, "es-US");
    g_assert_cmpfloat(f->language_preference, ==, 10);
    const gchar *containers[] = {"mkv", "mp4", "mp3", "flac"};
    for (guint i = 0; i < G_N_ELEMENTS(containers); i++) {
        g_autoptr(MdSelection) selection = md_select_format(v, containers[i], 0, NULL);
        g_assert_nonnull(selection);
        g_assert_cmpstr(selection->selector, ==, i < 2 ? "v+es" : "es");
        g_assert_cmpstr(selection->audio_language, ==, "es-US");
    }
    /* Equal preferences preserve the existing quality ranking. */
    f->language_preference = -1;
    ((MdFormat *)g_ptr_array_index(v->formats, 2))->language_preference = -1;
    g_autoptr(MdSelection) fallback = md_select_format(v, "mkv", 0, NULL);
    g_assert_cmpstr(fallback->selector, ==, "v+en");
}
static void display_order(void) {
    MdManager *m = md_manager_new(NULL, NULL);
    MdJob *done = md_manager_add(m, "https://example.com", "done", "/tmp", "mp4", 0);
    MdJob *waiting = md_manager_add(m, "https://example.com", "waiting", "/tmp", "mp4", 0);
    MdJob *first = md_manager_add(m, "https://example.com", "first", "/tmp", "mp4", 0);
    MdJob *second = md_manager_add(m, "https://example.com", "second", "/tmp", "mp4", 0);
    done->state = MD_COMPLETED;
    first->state = MD_DOWNLOADING;
    second->state = MD_CONVERTING;
    g_autoptr(GPtrArray) order = md_manager_display_order(m);
    g_assert_true(g_ptr_array_index(order, 0) == first);
    g_assert_true(g_ptr_array_index(order, 1) == second);
    g_assert_true(g_ptr_array_index(order, 2) == done);
    g_assert_true(g_ptr_array_index(m->jobs, 0) == done);
    g_clear_pointer(&order, g_ptr_array_unref);
    first->from_playlist = TRUE;
    second->from_playlist = TRUE;
    order = md_manager_display_order(m);
    for (guint i = 0; i < m->jobs->len; i++)
        g_assert_true(g_ptr_array_index(order, i) == g_ptr_array_index(m->jobs, i));
    g_clear_pointer(&order, g_ptr_array_unref);
    first->state = MD_COMPLETED;
    second->state = MD_COMPLETED;
    waiting->state = MD_ANALYZING;
    order = md_manager_display_order(m);
    g_assert_true(g_ptr_array_index(order, 0) == waiting);
    md_manager_free(m);
}
static void urls(void) {
    g_assert_true(md_url_valid("https://example.com/a?x=1"));
    g_assert_false(md_url_valid("file:///etc/passwd"));
    g_assert_false(md_url_valid("https://a b"));
    g_assert_false(md_url_valid("https://u:p@example.com"));
}
static void units(void) {
    g_autofree gchar *s = md_size(1024);
    g_assert_cmpstr(s, ==, "1.0 KiB");
    g_autofree gchar *unknown = md_size(-1);
    g_assert_cmpstr(unknown, ==, "—");
}
static void metadata(void) {
    g_autoptr(GError) e = NULL;
    g_autoptr(MdVideo) invalid = md_video_parse("[]", &e);
    g_assert_null(invalid);
    g_assert_nonnull(e);
    g_clear_error(&e);
    g_autoptr(MdVideo) v = md_video_parse(
        "{\"title\":\"Test\",\"duration\":null,\"formats\":[{\"format_id\":\"18\",\"ext\":\"mp4\","
        "\"height\":360,\"vcodec\":\"avc1\",\"acodec\":\"mp4a\"},{\"format_id\":\"137\",\"height\":"
        "1080,\"vcodec\":\"avc1\",\"acodec\":\"none\"},{\"format_id\":\"140\",\"vcodec\":\"none\","
        "\"acodec\":\"mp4a\"}]}",
        &e);
    g_assert_no_error(e);
    g_assert_cmpfloat(v->duration, ==, -1);
    g_autoptr(MdSelection) s = md_select_format(v, "mp4", 1080, &e);
    g_assert_no_error(e);
    g_assert_cmpstr(s->selector, ==, "137+140");
    g_assert_cmpint(s->processing, ==, MD_MERGE);
    g_autoptr(MdSelection) direct = md_select_format(v, "mp4", 360, &e);
    g_assert_cmpint(direct->processing, ==, MD_DIRECT);
    g_autoptr(MdSelection) missing = md_select_format(v, "mp4", 720, &e);
    g_assert_null(missing);
    g_assert_nonnull(e);
    g_clear_error(&e);
    g_autoptr(MdSelection) incompatible = md_select_format(v, "webm", 1080, &e);
    g_assert_null(incompatible);
    g_clear_error(&e);
    g_autoptr(MdSelection) audio = md_select_format(v, "flac", 0, &e);
    g_assert_no_error(e);
    g_assert_true(audio->audio);
    g_autoptr(GArray) heights = md_resolutions(v);
    g_assert_cmpuint(heights->len, ==, 2);
}
static void format_fallback(void) {
    g_autoptr(GError) e = NULL;
    g_autoptr(MdVideo) v = md_video_parse(
        "{\"title\":\"fallback\",\"formats\":[{\"format_id\":\"low\",\"ext\":\"mp4\",\"height\":"
        "360,\"vcodec\":\"avc1\",\"acodec\":\"mp4a\"},{\"format_id\":\"high\",\"ext\":\"mp4\","
        "\"height\":1080,\"vcodec\":\"avc1\",\"acodec\":\"none\"}]}",
        &e);
    g_assert_no_error(e);
    g_autoptr(MdSelection) s = md_select_format(v, "mp4", 0, &e);
    g_assert_no_error(e);
    g_assert_cmpstr(s->selector, ==, "low");
    g_ptr_array_remove_index(v->formats, 0);
    g_autoptr(MdSelection) silent = md_select_format(v, "mp4", 0, &e);
    g_assert_no_error(e);
    g_assert_cmpstr(silent->selector, ==, "high");
}
static void playlist(void) {
    g_autoptr(GError) e = NULL;
    g_autoptr(MdVideo) v = md_video_parse(
        "{\"title\":\"Playlist\",\"webpage_url\":\"https://example.com/"
        "list\",\"entries\":[null,{\"title\":\"Own "
        "video\",\"webpage_url\":\"https://example.com/list\",\"url\":\"https://example.com/"
        "own.mp4\"},{\"title\":\"Invalid\",\"url\":\"file:///etc/passwd\"}]}",
        &e);
    g_assert_no_error(e);
    g_assert_cmpuint(v->entries->len, ==, 1);
    MdVideo *entry = g_ptr_array_index(v->entries, 0);
    g_assert_cmpstr(entry->url, ==, "https://example.com/own.mp4");
    g_assert_cmpuint(entry->playlist_index, ==, 2);
    g_autoptr(MdVideo) indexed = md_video_parse(
        "{\"title\":\"List\",\"entries\":[{\"title\":\"Clip\","
        "\"url\":\"https://example.com/clip\",\"playlist_index\":7}]}", NULL);
    entry = g_ptr_array_index(indexed->entries, 0);
    g_assert_cmpuint(entry->playlist_index, ==, 7);
}
static void transient_retries(void) {
    g_assert_true(md_retryable_error("HTTP Error 429: Too Many Requests"));
    g_assert_true(md_retryable_error("HTTP Error 503: Service Unavailable"));
    g_assert_true(md_retryable_error("ERROR: Unable to download webpage: HTTP Error 503"));
    g_assert_true(md_retryable_error("Connection reset by peer"));
    g_assert_true(md_retryable_error("HTTP Error 403: Forbidden"));
    g_assert_false(md_retryable_error("Unable to download webpage: HTTP Error 404"));
    g_assert_false(md_retryable_error("Video unavailable"));
    g_assert_false(md_retryable_error("FFmpeg: Permission denied"));
    g_assert_false(md_retryable_error(NULL));
    g_assert_cmpuint(md_retry_delay(1), ==, 3);
    g_assert_cmpuint(md_retry_delay(2), ==, 6);
    g_assert_cmpuint(md_retry_delay(3), ==, 12);
    g_assert_cmpuint(md_retry_delay(4), ==, 24);
    g_assert_cmpuint(md_retry_delay(10), ==, 30);
}
static void final_formats(void) {
    g_autoptr(GError) error = NULL;
    const gchar *mp3 = "{\"format\":{\"format_name\":\"mp3\"},"
                       "\"streams\":[{\"codec_type\":\"audio\",\"codec_name\":\"mp3\"}]}";
    g_assert_true(md_output_matches(mp3, "mp3", &error));
    g_assert_false(md_output_matches(mp3, "mp4", &error));
    g_clear_error(&error);
    const gchar *video = "{\"format\":{\"format_name\":\"mov,mp4,m4a\"},"
                         "\"streams\":[{\"codec_type\":\"video\",\"codec_name\":\"h264\"},"
                         "{\"codec_type\":\"audio\",\"codec_name\":\"aac\"}]}";
    g_assert_true(md_output_matches(video, "mp4", &error));
    g_assert_false(md_output_matches(video, "mp3", &error));
    g_clear_error(&error);
    g_assert_false(md_output_matches(video, "m4a", &error));
    g_clear_error(&error);
    const gchar *wrong_codec = "{\"format\":{\"format_name\":\"mp3\"},"
                               "\"streams\":[{\"codec_type\":\"audio\",\"codec_name\":\"aac\"}]}";
    g_assert_false(md_output_matches(wrong_codec, "mp3", &error));
    g_clear_error(&error);
    const gchar *cover = "{\"format\":{\"format_name\":\"mp3\"},"
                         "\"streams\":[{\"codec_type\":\"audio\",\"codec_name\":\"mp3\"},"
                         "{\"codec_type\":\"video\",\"codec_name\":\"mjpeg\","
                         "\"disposition\":{\"attached_pic\":1}}]}";
    g_assert_true(md_output_matches(cover, "mp3", &error));
    g_assert_true(md_output_matches(
        "{\"format\":{\"format_name\":\"wav\"},\"streams\":["
        "{\"codec_type\":\"audio\",\"codec_name\":\"pcm_s24le\"}]}", "wav", &error));
    g_assert_true(md_output_matches(
        "{\"format\":{\"format_name\":\"mov,mp4,m4a\"},\"streams\":["
        "{\"codec_type\":\"audio\",\"codec_name\":\"alac\"}]}", "m4a", &error));
    g_assert_false(md_output_matches("{}", "mp3", &error));
    g_clear_error(&error);
    g_assert_false(md_output_matches("[]", "mp3", &error));
    g_clear_error(&error);
    g_assert_false(md_output_matches("invalid", "mp3", &error));
}
static void cancel_all(void) {
    MdManager *m = md_manager_new(NULL, NULL);
    MdJob *first = md_manager_add(m, "https://example.com", "one", "/tmp", "mp3", 0);
    MdJob *second = md_manager_add(m, "https://example.com", "two", "/tmp", "mp3", 0);
    MdJob *done = md_manager_add(m, "https://example.com", "done", "/tmp", "mp3", 0);
    done->state = MD_COMPLETED;
    md_manager_cancel_all(m);
    g_assert_cmpint(first->state, ==, MD_CANCELLED);
    g_assert_cmpint(second->state, ==, MD_CANCELLED);
    g_assert_cmpint(done->state, ==, MD_COMPLETED);
    g_assert_false(m->stopping);
    g_assert_cmpuint(m->active, ==, 0);
    MdJob *next = md_manager_add(m, "https://example.com", "next", "/tmp", "mp3", 0);
    g_assert_cmpint(next->state, ==, MD_QUEUED);
    md_manager_free(m);
}
static void output_numbering(void) {
    MdManager *m = md_manager_new(NULL, NULL);
    MdJob *j = md_manager_add(m, "https://example.com", "test", "/tmp", "mp4", 0);
    g_assert_cmpuint(j->retries, ==, 3);
    g_autofree gchar *plain = md_job_output_template(j);
    g_assert_cmpstr(plain, ==, "%(title).160B [%(id)s].%(ext)s");
    j->playlist_number = 7;
    g_autofree gchar *numbered = md_job_output_template(j);
    g_assert_cmpstr(numbered, ==, "007 - %(title).160B [%(id)s].%(ext)s");
    j->state = MD_CANCELLED;
    j->retries = 1;
    m->stopping = TRUE;
    md_job_retry(j);
    g_assert_cmpuint(j->playlist_number, ==, 7);
    g_assert_cmpuint(j->retries, ==, 1);
    md_manager_free(m);
}
static void errors(void) {
    g_autofree gchar *disk = md_error_message("No space left on device");
    g_assert_nonnull(strstr(disk, "espacio"));
    g_autofree gchar *permission = md_error_message("Permission denied");
    g_assert_nonnull(strstr(permission, "permisos"));
}
static void progress(void) {
    MdProgress p;
    g_assert_false(md_progress_parse("text", &p));
    g_assert_false(md_progress_parse("MD_PROGRESS:[]", &p));
    g_assert_true(md_progress_parse(
        "MD_PROGRESS:{\"downloaded_bytes\":135,\"total_bytes_estimate\":200,"
        "\"filename\":\"track.f140.m4a\",\"speed\":null,\"eta\":14,\"status\":\"downloading\"}",
        &p));
    g_assert_cmpstr(p.filename, ==, "track.f140.m4a");
    g_assert_cmpfloat(p.total, ==, 200);
    g_assert_true(p.estimated);
    g_assert_cmpfloat(p.speed, ==, -1);
    g_assert_cmpfloat(p.eta, ==, 14);
    md_progress_clear(&p);
}
static void versions(void) {
    g_assert_true(md_version_newer("2025.09.05", "2026.08.19"));
    g_assert_true(md_version_newer("1.9.9", "v1.10.0"));
    g_assert_false(md_version_newer("v1.10.0", "1.9.9"));
    g_assert_false(md_version_newer("1.0.0", "v1.0.0"));
    g_assert_false(md_version_newer("1.0.0", "1.0.1-beta"));
    g_assert_false(md_version_newer("nightly", "1.0.0"));
    g_assert_false(md_version_newer("1.0.0", "18446744073709551616.0.0"));
    g_assert_true(md_version_newer(NULL, "2026.08.19"));
}
static void releases(void) {
    g_autoptr(GError) error = NULL;
    const gchar *json =
        "{\"tag_name\":\"v1.2.0\",\"draft\":false,\"prerelease\":false,\"html_url\":\"https://"
        "github.com/ingferchorojas/media-downloader-gtk/releases/tag/v1.2.0\"}";
    g_autoptr(MdRelease) r = md_release_parse(json, MD_APP_REPOSITORY, FALSE, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(r->version, ==, "1.2.0");
    g_autoptr(MdRelease) missing = md_release_parse(json, MD_ENGINE_REPOSITORY, TRUE, &error);
    g_assert_null(missing);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error(&error);
    g_autoptr(MdRelease) foreign =
        md_release_parse("{\"tag_name\":\"v9.0.0\",\"html_url\":\"https://example.com/download\"}",
                         MD_APP_REPOSITORY, FALSE, &error);
    g_assert_null(foreign);
    g_clear_error(&error);
    g_autoptr(MdRelease) draft =
        md_release_parse("{\"tag_name\":\"v9.0.0\",\"draft\":true,\"html_url\":\"https://"
                         "github.com/ingferchorojas/media-downloader-gtk/releases/tag/v9.0.0\"}",
                         MD_APP_REPOSITORY, FALSE, &error);
    g_assert_null(draft);
    g_clear_error(&error);
    const gchar *engine =
        "{\"tag_name\":\"2026.08.19\",\"html_url\":\"https://github.com/yt-dlp/yt-dlp/releases/tag/"
        "2026.08.19\",\"assets\":[{\"name\":\"yt-dlp\",\"browser_download_url\":\"https://"
        "github.com/yt-dlp/yt-dlp/releases/download/2026.08.19/"
        "yt-dlp\"},{\"name\":\"SHA2-256SUMS\",\"browser_download_url\":\"https://github.com/yt-dlp/"
        "yt-dlp/releases/download/2026.08.19/SHA2-256SUMS\"}]}";
    g_autoptr(MdRelease) e = md_release_parse(engine, MD_ENGINE_REPOSITORY, TRUE, &error);
    g_assert_no_error(error);
    g_assert_nonnull(e->download_url);
    g_assert_nonnull(e->checksum_url);
}
static void checksums(void) {
    g_autoptr(GBytes) bytes = g_bytes_new_static("abc", 3);
    g_assert_true(md_checksum_matches(
        bytes, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    g_assert_false(md_checksum_matches(
        bytes, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ae"));
    g_assert_false(md_checksum_matches(bytes, "invalid"));
}
static void atomic_update(void) {
    g_autofree gchar *path = md_engine_managed_path(), *dir = g_path_get_dirname(path);
    g_assert_cmpint(g_mkdir_with_parents(dir, 0700), ==, 0);
    g_assert_true(g_file_set_contents(path, "previous version", -1, NULL));
    const gchar fixture[] = "#!/bin/sh\nprintf '2026.08.19\\n'\n";
    g_autoptr(GBytes) bytes = g_bytes_new_static(fixture, sizeof fixture - 1);
    g_autofree gchar *digest = g_compute_checksum_for_bytes(G_CHECKSUM_SHA256, bytes);
    g_autoptr(GError) error = NULL;
    g_autofree gchar *bad = md_engine_install(bytes, "bad checksum", "2026.08.19", NULL, &error);
    g_assert_null(bad);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error(&error);
    g_autofree gchar *mismatch = md_engine_install(bytes, digest, "2026.08.20", NULL, &error);
    g_assert_null(mismatch);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error(&error);
    g_autoptr(GCancellable) cancel = g_cancellable_new();
    g_cancellable_cancel(cancel);
    g_autofree gchar *cancelled = md_engine_install(bytes, digest, "2026.08.19", cancel, &error);
    g_assert_null(cancelled);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_clear_error(&error);
    g_autofree gchar *old = NULL;
    g_assert_true(g_file_get_contents(path, &old, NULL, NULL));
    g_assert_cmpstr(old, ==, "previous version");
    g_autofree gchar *version = md_engine_install(bytes, digest, "2026.08.19", NULL, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(version, ==, "2026.08.19");
    g_autofree gchar *selected = md_engine_path();
    g_assert_cmpstr(selected, ==, path);
    const gchar *args[] = {"yt-dlp", "--", "https://example.com", NULL};
    g_auto(GStrv) argv = md_engine_argv(args);
    g_assert_cmpstr(argv[0], ==, path);
    GDir *listing = g_dir_open(dir, 0, NULL);
    guint count = 0;
    while (g_dir_read_name(listing))
        count++;
    g_dir_close(listing);
    g_assert_cmpuint(count, ==, 1);
    g_unlink(path);
    g_rmdir(dir);
    g_autofree gchar *parent = g_path_get_dirname(dir);
    g_rmdir(parent);
    g_autofree gchar *data = g_path_get_dirname(parent);
    g_rmdir(data);
}
static void queue(void) {
    MdManager *m = md_manager_new(NULL, NULL);
    m->limit = 0;
    MdJob *a = md_manager_add(m, "https://example.com", "a", "/tmp", "mp4", 0);
    md_manager_schedule(m);
    g_assert_cmpint(a->state, ==, MD_QUEUED);
    g_assert_false(md_manager_remove(m, a));
    md_job_cancel(a);
    g_assert_cmpint(a->state, ==, MD_CANCELLED);
    md_job_retry(a);
    g_assert_cmpint(a->state, ==, MD_QUEUED);
    md_manager_stop(m);
    g_assert_true(md_manager_remove(m, a));
    md_manager_free(m);
}
typedef struct {
    GMainLoop *loop;
    MdProcess *process;
    gboolean success, done, cancel, delayed;
    guint lines;
} ProcessTest;
static const gchar *test_program;
static void test_line(const gchar *line, gpointer data) {
    ProcessTest *t = data;
    (void)line;
    t->lines++;
    if (t->cancel && !t->delayed)
        md_process_cancel(t->process);
}
static void test_done(gboolean ok, const gchar *error, gpointer data) {
    ProcessTest *t = data;
    t->success = ok;
    t->done = TRUE;
    if (!ok)
        g_assert_nonnull(error);
    g_main_loop_quit(t->loop);
}
static gboolean process_timeout(gpointer data) {
    (void)data;
    g_error("Process test timed out");
    return G_SOURCE_REMOVE;
}
static gboolean delayed_cancel(gpointer data) {
    md_process_cancel(((ProcessTest *)data)->process);
    return G_SOURCE_REMOVE;
}
static void process_case(const gchar *mode, gboolean success, gboolean cancel) {
    ProcessTest t = {0};
    t.loop = g_main_loop_new(NULL, FALSE);
    t.cancel = cancel;
    t.delayed = g_str_equal(mode, "--helper-orphan");
    const gchar *argv[] = {test_program, mode, NULL};
    g_autoptr(GError) e = NULL;
    t.process = md_process_start(argv, test_line, test_done, &t, &e);
    g_assert_no_error(e);
    g_assert_nonnull(t.process);
    guint deferred = t.delayed ? g_timeout_add(100, delayed_cancel, &t) : 0;
    guint timeout = g_timeout_add_seconds(5, process_timeout, NULL);
    g_main_loop_run(t.loop);
    g_source_remove(timeout);
    if (deferred && g_main_context_find_source_by_id(NULL, deferred))
        g_source_remove(deferred);
    g_assert_true(t.done);
    g_assert_cmpint(t.success, ==, success);
    g_assert_cmpuint(t.lines, >, 0);
    md_process_free(t.process);
    g_main_loop_unref(t.loop);
}
static void process_success(void) {
    process_case("--helper-ok", TRUE, FALSE);
}
static void process_failure(void) {
    process_case("--helper-fail", FALSE, FALSE);
}
static void process_cancel(void) {
    process_case("--helper-wait", FALSE, TRUE);
}
static void process_group(void) {
    process_case("--helper-orphan", FALSE, TRUE);
}
static void preferences(void) {
    MdPreferences *p = md_preferences_load();
    g_assert_cmpuint(p->retries, ==, 3);
    g_assert_false(p->number_playlist);
    p->concurrency = 3;
    p->retries = 2;
    p->number_playlist = TRUE;
    g_free(p->theme);
    p->theme = g_strdup("dark");
    g_autoptr(GError) e = NULL;
    g_assert_true(md_preferences_save(p, &e));
    g_assert_no_error(e);
    MdPreferences *loaded = md_preferences_load();
    g_assert_cmpuint(loaded->concurrency, ==, 3);
    g_assert_cmpstr(loaded->theme, ==, "dark");
    g_assert_cmpuint(loaded->retries, ==, 2);
    g_assert_true(loaded->number_playlist);
    loaded->retries = 0;
    g_assert_true(md_preferences_save(loaded, &e));
    md_preferences_free(loaded);
    loaded = md_preferences_load();
    g_assert_cmpuint(loaded->retries, ==, 1);
    loaded->retries = 11;
    g_assert_true(md_preferences_save(loaded, &e));
    md_preferences_free(loaded);
    loaded = md_preferences_load();
    g_assert_cmpuint(loaded->retries, ==, 10);
    md_preferences_free(p);
    md_preferences_free(loaded);
}
int main(int argc, char **argv) {
    if (argc == 2 && g_str_has_prefix(argv[1], "--helper-")) {
        if (g_str_equal(argv[1], "--helper-orphan")) {
            pid_t pid = fork();
            if (pid < 0)
                return 2;
            if (pid == 0) {
                puts("descendant");
                fflush(stdout);
                sleep(30);
                _exit(0);
            }
            return 0;
        }
        puts("stdout");
        fputs("stderr\n", stderr);
        fflush(stdout);
        if (g_str_equal(argv[1], "--helper-wait")) {
            sleep(30);
            return 0;
        }
        return g_str_equal(argv[1], "--helper-fail") ? 7 : 0;
    }
    g_autofree gchar *config = g_dir_make_tmp("md-test-config-XXXXXX", NULL);
    g_setenv("XDG_CONFIG_HOME", config, TRUE);
    g_autofree gchar *data_dir = g_build_filename(config, "data", NULL);
    g_setenv("XDG_DATA_HOME", data_dir, TRUE);
    test_program = argv[0];
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/utils/url", urls);
    g_test_add_func("/utils/units", units);
    g_test_add_func("/metadata/formats", metadata);
    g_test_add_func("/formats/original-audio", original_audio);
    g_test_add_func("/formats/fallback-and-silent", format_fallback);
    g_test_add_func("/metadata/playlist", playlist);
    g_test_add_func("/errors/messages", errors);
    g_test_add_func("/download/transient-retries", transient_retries);
    g_test_add_func("/download/final-formats", final_formats);
    g_test_add_func("/download/progress", progress);
    g_test_add_func("/download/weighted", weighted_progress);
    g_test_add_func("/download/monotonic", monotonic_progress);
    g_test_add_func("/download/fragment-estimate", fragment_progress);
    g_test_add_func("/updates/version-order", versions);
    g_test_add_func("/updates/releases", releases);
    g_test_add_func("/updates/checksums", checksums);
    g_test_add_func("/updates/atomic-install-rollback", atomic_update);
    g_test_add_func("/manager/output-numbering", output_numbering);
    g_test_add_func("/manager/cancel-all", cancel_all);
    g_test_add_func("/manager/queue", queue);
    g_test_add_func("/manager/display-order", display_order);
    g_test_add_func("/process/success", process_success);
    g_test_add_func("/process/failure", process_failure);
    g_test_add_func("/process/cancel", process_cancel);
    g_test_add_func("/process/group-after-parent-exit", process_group);
    g_test_add_func("/preferences/persistence", preferences);
    gint code = g_test_run();
    g_autofree gchar *file =
        g_build_filename(config, "media-downloader-gtk", "preferences.ini", NULL);
    g_autofree gchar *dir = g_path_get_dirname(file);
    g_unlink(file);
    g_rmdir(dir);
    g_rmdir(config);
    return code;
}
