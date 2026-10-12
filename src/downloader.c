#include "downloader.h"
#include "utils.h"
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
struct _MdProcess {
    GSubprocess *child;
    GDataInputStream *output;
    MdLineFunc line;
    MdDoneFunc done;
    gpointer data;
    pid_t group;
    gboolean eof, waited, success, cancelled;
    gchar *error;
};
static void child_setup(gpointer data) {
    (void)data;
    setsid();
}
static GSubprocessLauncher *launcher(GSubprocessFlags flags) {
    GSubprocessLauncher *l = g_subprocess_launcher_new(flags);
    g_subprocess_launcher_set_child_setup(l, child_setup, NULL, NULL);
    return l;
}
static void kill_group(GSubprocess *child) {
    const gchar *id = g_subprocess_get_identifier(child);
    if (id) {
        pid_t pid = (pid_t)g_ascii_strtoll(id, NULL, 10);
        if (pid > 1)
            kill(-pid, SIGKILL);
    }
    g_subprocess_force_exit(child);
}
static void finish(MdProcess *p) {
    if (p->eof && p->waited) {
        MdDoneFunc done = p->done;
        p->done = NULL;
        if (done)
            done(p->success && !p->cancelled && !p->error,
                 p->cancelled ? "Operación cancelada." : p->error, p->data);
    }
}
static void read_line(GObject *source, GAsyncResult *result, gpointer data) {
    MdProcess *p = data;
    g_autoptr(GError) error = NULL;
    gsize length;
    g_autofree gchar *line =
        g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(source), result, &length, &error);
    if (!line) {
        p->eof = TRUE;
        if (error && !p->error)
            p->error = g_strdup(error->message);
        finish(p);
        return;
    }
    if (length <= 65536 && g_utf8_validate(line, -1, NULL) && p->line)
        p->line(line, p->data);
    g_data_input_stream_read_line_async(p->output, G_PRIORITY_DEFAULT, NULL, read_line, p);
}
static void waited(GObject *source, GAsyncResult *result, gpointer data) {
    MdProcess *p = data;
    g_autoptr(GError) error = NULL;
    p->success = g_subprocess_wait_check_finish(G_SUBPROCESS(source), result, &error);
    p->waited = TRUE;
    if (error && !p->error)
        p->error = g_strdup(error->message);
    finish(p);
}
MdProcess *md_process_start(const gchar *const *argv, MdLineFunc line, MdDoneFunc done,
                            gpointer data, GError **error) {
    g_autoptr(GSubprocessLauncher) l =
        launcher(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
    GSubprocess *child = g_subprocess_launcher_spawnv(l, argv, error);
    if (!child)
        return NULL;
    MdProcess *p = g_new0(MdProcess, 1);
    p->child = child;
    p->group = (pid_t)g_ascii_strtoll(g_subprocess_get_identifier(child), NULL, 10);
    p->line = line;
    p->done = done;
    p->data = data;
    p->output = g_data_input_stream_new(g_subprocess_get_stdout_pipe(child));
    g_data_input_stream_set_newline_type(p->output, G_DATA_STREAM_NEWLINE_TYPE_ANY);
    g_data_input_stream_read_line_async(p->output, G_PRIORITY_DEFAULT, NULL, read_line, p);
    g_subprocess_wait_check_async(child, NULL, waited, p);
    return p;
}
void md_process_cancel(MdProcess *p) {
    if (p && (!p->waited || !p->eof)) {
        p->cancelled = TRUE;
        if (p->group > 1)
            kill(-p->group, SIGKILL);
        if (!p->waited)
            g_subprocess_force_exit(p->child);
    }
}
void md_process_free(MdProcess *p) {
    if (!p)
        return;
    g_assert(p->eof && p->waited);
    g_object_unref(p->output);
    g_object_unref(p->child);
    g_free(p->error);
    g_free(p);
}
typedef struct {
    GSubprocess *child;
    GCancellable *cancel;
    gulong handler;
    MdAnalysisFunc callback;
    gpointer data;
} Analysis;
static void analysis_cancelled(GCancellable *cancel, gpointer data) {
    (void)cancel;
    kill_group(((Analysis *)data)->child);
}
static void analysis_done(GObject *source, GAsyncResult *result, gpointer data) {
    Analysis *a = data;
    g_autofree gchar *out = NULL, *err = NULL;
    g_autoptr(GError) error = NULL;
    MdVideo *v = NULL;
    gboolean ok =
        g_subprocess_communicate_utf8_finish(G_SUBPROCESS(source), result, &out, &err, &error);
    if (a->handler)
        g_cancellable_disconnect(a->cancel, a->handler);
    if (a->cancel && g_cancellable_is_cancelled(a->cancel)) {
        g_clear_error(&error);
        g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Análisis cancelado.");
    } else if (ok && g_subprocess_get_successful(a->child))
        v = md_video_parse(out, &error);
    else if (!error)
        g_set_error(&error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s",
                    err && *err ? err : "yt-dlp no pudo analizar el contenido.");
    a->callback(v, error ? error->message : NULL, a->data);
    g_clear_object(&a->cancel);
    g_object_unref(a->child);
    g_free(a);
}
void md_analyze_async(const gchar *url, GCancellable *cancel, MdAnalysisFunc callback,
                      gpointer data) {
    g_autoptr(GError) error = NULL;
    if (!md_url_valid(url)) {
        callback(NULL, "Introduce una URL HTTP o HTTPS válida.", data);
        return;
    }
    const gchar *argv[] = {"yt-dlp",
                           "--ignore-config",
                           "--dump-single-json",
                           "--skip-download",
                           "--flat-playlist",
                           "--playlist-end",
                           "100",
                           "--socket-timeout",
                           "20",
                           "--",
                           url,
                           NULL};
    g_autoptr(GSubprocessLauncher) l =
        launcher(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
    GSubprocess *child = g_subprocess_launcher_spawnv(l, argv, &error);
    if (!child) {
        callback(NULL, error->message, data);
        return;
    }
    Analysis *a = g_new0(Analysis, 1);
    a->child = child;
    a->callback = callback;
    a->data = data;
    if (cancel) {
        a->cancel = g_object_ref(cancel);
        a->handler = g_cancellable_connect(cancel, G_CALLBACK(analysis_cancelled), a, NULL);
    }
    /* Do not cancel communicate: drain and reap the killed process first. */
    g_subprocess_communicate_utf8_async(child, NULL, NULL, analysis_done, a);
}
static gint audio_encoders;
gboolean md_audio_conversion_supported(const gchar *container) {
    const gchar *formats[] = {"mp3", "m4a", "opus", "flac", "wav"};
    for (guint i = 0; i < 5; i++)
        if (g_str_equal(container, formats[i]))
            return (g_atomic_int_get(&audio_encoders) & (1 << i)) != 0;
    return TRUE;
}
static void dependency_worker(GTask *task, gpointer source, gpointer task_data,
                              GCancellable *cancel) {
    (void)source;
    (void)task_data;
    (void)cancel;
    const gchar *programs[] = {"yt-dlp", "ffmpeg"};
    for (guint i = 0; i < 2; i++) {
        g_autoptr(GError) error = NULL;
        g_autoptr(GSubprocess) p =
            g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
                             &error, programs[i], i == 0 ? "--version" : "-version", NULL);
        if (!p || !g_subprocess_wait_check(p, NULL, &error)) {
            g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                                    "No se puede ejecutar %s. Instala las dependencias con: sudo "
                                    "apt install yt-dlp ffmpeg",
                                    programs[i]);
            return;
        }
    }
    g_autoptr(GError) error = NULL;
    g_autoptr(GSubprocess) encoders =
        g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, &error,
                         "ffmpeg", "-encoders", NULL);
    g_autofree gchar *out = NULL;
    if (!encoders || !g_subprocess_communicate_utf8(encoders, NULL, NULL, &out, NULL, &error) ||
        !g_subprocess_get_successful(encoders)) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED,
                                "No se pudieron comprobar los codificadores de FFmpeg.");
        return;
    }
    const gchar *names[] = {"libmp3lame", " aac ", "libopus", " flac ", "pcm_s16le"};
    gint mask = 0;
    for (guint i = 0; i < 5; i++)
        if (strstr(out, names[i]))
            mask |= 1 << i;
    g_atomic_int_set(&audio_encoders, mask);
    g_task_return_boolean(task, TRUE);
}
void md_dependencies_async(GAsyncReadyCallback callback, gpointer data) {
    GTask *t = g_task_new(NULL, NULL, callback, data);
    g_task_run_in_thread(t, dependency_worker);
    g_object_unref(t);
}
gboolean md_dependencies_finish(GAsyncResult *r, GError **error) {
    return g_task_propagate_boolean(G_TASK(r), error);
}
gboolean md_progress_parse(const gchar *line, MdProgress *p) {
    if (!g_str_has_prefix(line, "MD_PROGRESS:"))
        return FALSE;
    g_autoptr(JsonParser) parser = json_parser_new();
    if (!json_parser_load_from_data(parser, line + 12, -1, NULL))
        return FALSE;
    JsonNode *root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root))
        return FALSE;
    JsonObject *o = json_node_get_object(root);
    memset(p, 0, sizeof *p);
    p->downloaded = md_json_number(o, "downloaded_bytes", -1);
    p->total = md_json_number(o, "total_bytes", -1);
    if (p->total <= 0)
        p->total = md_json_number(o, "total_bytes_estimate", -1);
    p->speed = md_json_number(o, "speed", -1);
    p->eta = md_json_number(o, "eta", -1);
    p->status = g_strdup(md_json_string(o, "status"));
    p->stage = g_strdup(md_json_string(o, "postprocessor"));
    gdouble fragment = md_json_number(o, "fragment_index", 0),
            fragments = md_json_number(o, "fragment_count", 0);
    p->fragment = fragment > 0 && fragment < G_MAXINT ? (gint)fragment : 0;
    p->fragments = fragments > 0 && fragments < G_MAXINT ? (gint)fragments : 0;
    return TRUE;
}
void md_progress_clear(MdProgress *p) {
    g_free(p->status);
    g_free(p->stage);
}
