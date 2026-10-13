#include "download-manager.h"
#include "utils.h"
#include <glib/gstdio.h>
const gchar *md_state_name(MdState state) {
    const gchar *names[] = {"En espera",
                            "Analizando",
                            "Descargando",
                            "Procesando",
                            "Combinando audio y video",
                            "Convirtiendo formato",
                            "Completado",
                            "Cancelado",
                            "Error"};
    return state <= MD_ERROR ? names[state] : "Error";
}
gboolean md_state_terminal(MdState state) {
    return state == MD_COMPLETED || state == MD_CANCELLED || state == MD_ERROR;
}
static void changed(MdJob *j) {
    if (j->manager->changed)
        j->manager->changed(j, j->manager->data);
}
static void job_free(gpointer data) {
    MdJob *j = data;
    g_assert(!j->process);
    g_assert(!j->retry_timer);
    g_hash_table_unref(j->track_progress);
    g_free(j->url);
    g_free(j->title);
    g_free(j->directory);
    g_free(j->container);
    g_free(j->error);
    g_free(j->detail);
    g_free(j->work_directory);
    g_free(j->output_path);
    g_strfreev(j->download_argv);
    g_clear_object(&j->cancel);
    g_free(j);
}
MdManager *md_manager_new(void (*callback)(MdJob *, gpointer), gpointer data) {
    MdManager *m = g_new0(MdManager, 1);
    m->jobs = g_ptr_array_new_with_free_func(job_free);
    m->limit = 2;
    m->changed = callback;
    m->data = data;
    return m;
}
MdJob *md_manager_add(MdManager *m, const gchar *url, const gchar *title, const gchar *directory,
                      const gchar *container, gint height) {
    MdJob *j = g_new0(MdJob, 1);
    j->manager = m;
    j->url = g_strdup(url);
    j->title = g_strdup(title);
    j->directory = g_strdup(directory);
    j->container = g_strdup(container);
    j->height = height;
    j->retries = 3;
    j->cancel = g_cancellable_new();
    j->fraction = -1;
    j->expected_tracks = 1;
    j->track_progress = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    g_ptr_array_add(m->jobs, j);
    return j;
}
static void ended(MdJob *j, gboolean ok, const gchar *error) {
    if (g_cancellable_is_cancelled(j->cancel))
        j->state = MD_CANCELLED;
    else if (ok) {
        j->state = MD_COMPLETED;
        j->fraction = 1;
        g_free(j->detail);
        j->detail = g_strdup_printf("Archivo guardado en %s", j->directory);
    } else {
        j->state = MD_ERROR;
        if (!j->error)
            j->error = md_error_message(error);
    }
    j->manager->active--;
    changed(j);
    md_manager_schedule(j->manager);
}
static gboolean verify_output(const gchar *path, const gchar *container,
                              GCancellable *cancel, GError **error) {
    g_autofree gchar *extension = g_strconcat(".", container, NULL);
    if (!path || !g_str_has_suffix(path, extension) ||
        !g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                    "No se obtuvo un archivo final válido en formato %s.", container);
        return FALSE;
    }
    g_autoptr(GSubprocess) probe = g_subprocess_new(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE, error,
        "ffprobe", "-v", "error", "-show_entries",
        "format=format_name:stream=codec_type,codec_name:stream_disposition=attached_pic",
        "-of", "json", path, NULL);
    g_autofree gchar *out = NULL, *err = NULL;
    if (!probe)
        return FALSE;
    if (!g_subprocess_communicate_utf8(probe, NULL, cancel, &out, &err, error)) {
        g_subprocess_force_exit(probe);
        return FALSE;
    }
    if (!g_subprocess_get_successful(probe)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                    "FFmpeg no pudo verificar el archivo final: %s", err ? err : "");
        return FALSE;
    }
    return md_output_matches(out, container, error);
}
static void finalize_worker(GTask *task, gpointer source, gpointer task_data, GCancellable *cancel) {
    (void)source;
    MdJob *j = task_data;
    g_autoptr(GError) error = NULL;
    if (!j->output_path || !j->work_directory) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "No se recibió el archivo final de la descarga.");
        return;
    }
    g_autofree gchar *source_dir = g_path_get_dirname(j->output_path);
    if (g_strcmp0(source_dir, j->work_directory) != 0 ||
        !verify_output(j->output_path, j->container, cancel, &error)) {
        if (!error)
            g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "El archivo final está fuera de la carpeta temporal.");
        g_task_return_error(task, g_steal_pointer(&error));
        return;
    }
    g_autofree gchar *name = g_path_get_basename(j->output_path);
    g_autofree gchar *destination = g_build_filename(j->directory, name, NULL);
    g_autoptr(GFile) from = g_file_new_for_path(j->output_path);
    g_autoptr(GFile) to = g_file_new_for_path(destination);
    if (!g_file_move(from, to, G_FILE_COPY_NONE, cancel, NULL, NULL, &error)) {
        if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
            g_task_return_error(task, g_steal_pointer(&error));
            return;
        }
        g_clear_error(&error);
        if (!verify_output(destination, j->container, cancel, &error)) {
            g_task_return_error(task, g_steal_pointer(&error));
            return;
        }
        g_unlink(j->output_path);
    }
    /* Remove only files in this job's private staging directory after success. */
    GDir *dir = g_dir_open(j->work_directory, 0, NULL);
    if (dir) {
        const gchar *entry;
        while ((entry = g_dir_read_name(dir))) {
            g_autofree gchar *path = g_build_filename(j->work_directory, entry, NULL);
            if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
                g_unlink(path);
        }
        g_dir_close(dir);
    }
    g_rmdir(j->work_directory);
    g_task_return_boolean(task, TRUE);
}
static void process_line(const gchar *line, gpointer data);
static void process_done(gboolean ok, const gchar *error, gpointer data);
static void metadata_ready(MdVideo *v, const gchar *error, gpointer data);
guint md_retry_delay(guint attempt) {
    return MIN(30u, 3u << MIN(attempt > 0 ? attempt - 1 : 0, 4u));
}
gboolean md_retryable_error(const gchar *error) {
    if (!error)
        return FALSE;
    g_autofree gchar *text = g_ascii_strdown(error, -1);
    const gchar *temporary[] = {"timed out", "timeout", "temporarily", "temporary failure",
        "connection reset", "connection refused", "connection aborted", "network is unreachable",
        "remote end closed", "remotedisconnected", "unable to download", "http error 403",
        "http error 408", "http error 429", "http error 500", "http error 502", "http error 503",
        "http error 504", "too many requests", "incomplete read", NULL};
    const gchar *permanent[] = {"http error 404", "http error 410", "video not available", "video unavailable",
        "video is unavailable", "this video is not available",
        "private video", "sign in", "login required", "unsupported url", "permission denied",
        "no space left", NULL};
    for (guint i = 0; permanent[i]; i++)
        if (strstr(text, permanent[i]))
            return FALSE;
    for (guint i = 0; temporary[i]; i++)
        if (strstr(text, temporary[i]))
            return TRUE;
    return FALSE;
}
static gboolean resume_retry(gpointer data) {
    MdJob *j = data;
    j->retry_timer = 0;
    if (g_cancellable_is_cancelled(j->cancel) || j->manager->stopping) {
        ended(j, FALSE, "Cancelado");
        return G_SOURCE_REMOVE;
    }
    if (j->retry_analysis) {
        md_analyze_async(j->url, j->cancel, metadata_ready, j);
    } else {
        g_autoptr(GError) error = NULL;
        j->process = md_process_start((const gchar *const *)j->download_argv,
                                      process_line, process_done, j, &error);
        if (!j->process)
            ended(j, FALSE, error->message);
    }
    return G_SOURCE_REMOVE;
}
static gboolean schedule_retry(MdJob *j, gboolean analysis) {
    guint *counter = analysis ? &j->transfer_retries : &j->processing_retries;
    if (g_cancellable_is_cancelled(j->cancel) || j->manager->stopping ||
        *counter >= CLAMP(j->retries, 1u, 10u))
        return FALSE;
    (*counter)++;
    guint delay = md_retry_delay(*counter);
    g_clear_pointer(&j->error, g_free);
    g_clear_pointer(&j->output_path, g_free);
    g_free(j->detail);
    j->detail = g_strdup_printf("Reintento %u/%u de %s en %u segundos…", *counter,
                                CLAMP(j->retries, 1u, 10u),
                                analysis ? "conexión" : "procesamiento", delay);
    j->state = analysis ? MD_ANALYZING : MD_CONVERTING;
    j->retry_analysis = analysis;
    j->retry_timer = g_timeout_add_seconds(delay, resume_retry, j);
    changed(j);
    return TRUE;
}
static gboolean retry_processing(MdJob *j) {
    return j->processing_started && j->download_argv && schedule_retry(j, FALSE);
}
static gboolean retry_transfer(MdJob *j, const gchar *error) {
    return md_retryable_error(error) && schedule_retry(j, TRUE);
}
static void finalized(GObject *source, GAsyncResult *result, gpointer data) {
    (void)source;
    MdJob *j = data;
    g_autoptr(GError) error = NULL;
    gboolean ok = g_task_propagate_boolean(G_TASK(result), &error);
    if (!ok && retry_processing(j))
        return;
    if (ok)
        g_clear_pointer(&j->work_directory, g_free);
    ended(j, ok, error ? error->message : NULL);
}
static void process_done(gboolean ok, const gchar *error, gpointer data) {
    MdJob *j = data;
    g_autofree gchar *detail = g_strdup(error);
    md_process_free(j->process);
    j->process = NULL;
    if (!ok || g_cancellable_is_cancelled(j->cancel)) {
        if (retry_processing(j) || retry_transfer(j, detail))
            return;
        ended(j, FALSE, detail);
        return;
    }
    j->state = MD_PROCESSING;
    g_free(j->detail);
    j->detail = g_strdup("Verificando el formato del archivo final…");
    changed(j);
    GTask *task = g_task_new(NULL, j->cancel, finalized, j);
    g_task_set_task_data(task, j, NULL);
    g_task_run_in_thread(task, finalize_worker);
    g_object_unref(task);
}
typedef struct {
    gdouble fraction, downloaded, total;
    gboolean estimated, finished;
} MdTrackProgress;

void md_job_prepare_tracks(MdJob *j, const MdSelection *s) {
    g_hash_table_remove_all(j->track_progress);
    j->expected_tracks = s->tracks;
    j->downloaded_bytes = 0;
    j->total_bytes = 0;
    j->total_estimated = FALSE;
    for (guint i = 0; i < s->tracks; i++) {
        MdTrackProgress *track = g_new0(MdTrackProgress, 1);
        track->total = s->track_sizes[i];
        track->estimated = s->track_estimated[i];
        g_hash_table_insert(j->track_progress, g_strdup(s->track_ids[i]), track);
        if (track->total > 0)
            j->total_bytes += track->total;
        j->total_estimated |= track->estimated;
    }
    for (guint i = 0; i < s->tracks; i++)
        if (s->track_sizes[i] <= 0)
            j->total_bytes = -1;
}

void md_job_apply_progress(MdJob *j, const MdProgress *p) {
    if (p->stage) {
        j->fraction = MAX(j->fraction, 0.99);
        return;
    }
    const gchar *key = p->format_id ? p->format_id : (p->filename ? p->filename : "download");
    MdTrackProgress *track = g_hash_table_lookup(j->track_progress, key);
    if (!track) {
        track = g_new0(MdTrackProgress, 1);
        g_hash_table_insert(j->track_progress, g_strdup(key), track);
    }
    track->downloaded = MAX(track->downloaded, MAX(0, p->downloaded));
    /* Keep the first usable estimate. Initialization fragments are too small
     * to estimate the entire stream, so wait for several completed fragments. */
    if (p->total > 0 && (!p->estimated ||
        (track->total <= 0 && (p->fragments <= 0 || p->fragment >= 5)))) {
        track->total = p->total;
        track->estimated = p->estimated;
    }
    if (g_strcmp0(p->status, "finished") == 0) {
        track->finished = TRUE;
        track->fraction = 1;
        if (p->downloaded <= 0 && track->total > 0)
            track->downloaded = track->total;
        if (p->downloaded > 0) {
            track->total = track->downloaded;
            track->estimated = FALSE;
        }
    } else if (p->estimated && p->fragments > 0)
        track->fraction = MAX(track->fraction, CLAMP((gdouble)p->fragment / p->fragments, 0, 1));
    else if (p->total > 0 && p->downloaded >= 0)
        track->fraction = MAX(track->fraction, CLAMP(p->downloaded / p->total, 0, 1));

    gdouble sum = 0, downloaded = 0, total = 0;
    gboolean known = g_hash_table_size(j->track_progress) == j->expected_tracks;
    gboolean estimated = FALSE, finished = known;
    GHashTableIter iter;
    gpointer value;
    g_hash_table_iter_init(&iter, j->track_progress);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        MdTrackProgress *t = value;
        sum += t->fraction;
        downloaded += t->downloaded;
        total += t->total;
        known &= t->total > 0;
        estimated |= t->estimated;
        finished &= t->finished;
    }
    j->downloaded_bytes = downloaded;
    /* Freeze the combined denominator and its approximation marker until all
     * tracks finish, even if intermediate events provide revised sizes. */
    if (known && (j->total_bytes <= 0 || finished)) {
        j->total_bytes = total;
        j->total_estimated = estimated;
    } else if (known && j->total_bytes > 0 && total != j->total_bytes) {
        j->total_estimated = TRUE;
    }
    gdouble fraction = j->total_bytes > 0 ? downloaded / j->total_bytes
                                         : sum / MAX(1u, j->expected_tracks);
    j->fraction = MAX(j->fraction, MIN(0.99, fraction));
}
static void process_line(const gchar *line, gpointer data) {
    MdJob *j = data;
    MdProgress p;
    if (g_str_has_prefix(line, "MD_OUTPUT:")) {
        j->processing_started = TRUE;
        g_autoptr(JsonParser) parser = json_parser_new();
        if (json_parser_load_from_data(parser, line + 10, -1, NULL)) {
            JsonNode *root = json_parser_get_root(parser);
            if (JSON_NODE_HOLDS_VALUE(root) && json_node_get_value_type(root) == G_TYPE_STRING) {
                g_free(j->output_path);
                j->output_path = g_strdup(json_node_get_string(root));
            }
        }
    } else if (md_progress_parse(line, &p)) {
        md_job_apply_progress(j, &p);
        if (p.stage) {
            j->processing_started = TRUE;
            j->state = g_strrstr(p.stage, "Merger") ? MD_MERGING
                       : (g_strrstr(p.stage, "ExtractAudio") || g_strrstr(p.stage, "Convert"))
                           ? MD_CONVERTING
                           : MD_PROCESSING;
            g_free(j->detail);
            j->detail = g_strdup(j->state == MD_MERGING ? "Combinando las pistas descargadas…"
                                 : j->state == MD_CONVERTING ? "Convirtiendo el archivo multimedia…"
                                                             : "Preparando el archivo final…");
        } else {
            j->state = g_strcmp0(p.status, "finished") == 0 ? MD_PROCESSING : MD_DOWNLOADING;
            g_autofree gchar *down = md_size(j->downloaded_bytes),
                             *total = md_size(j->total_bytes), *speed = md_size(p.speed);
            gdouble remaining = j->total_bytes > 0 && p.speed > 0
                                    ? MAX(0, j->total_bytes - j->downloaded_bytes) / p.speed : -1;
            g_autofree gchar *eta = md_duration(remaining);
            g_free(j->detail);
            j->detail = g_strdup_printf("Descargado: %s / Total: %s%s · %s%s · Restante: %s", down,
                                        j->total_estimated && j->total_bytes > 0 ? "≈ " : "",
                                        total, speed, p.speed >= 0 ? "/s" : "", eta);
        }
        md_progress_clear(&p);
        changed(j);
    } else if (g_str_has_prefix(line, "ERROR:")) {
        g_free(j->error);
        j->error = md_error_message(line + 6);
        changed(j);
    }
}
gchar *md_job_output_template(const MdJob *j) {
    const gchar *base = "%(title).160B [%(id)s].%(ext)s";
    return j->playlist_number > 0 ? g_strdup_printf("%03u - %s", j->playlist_number, base)
                                  : g_strdup(base);
}
static void metadata_ready(MdVideo *v, const gchar *error, gpointer data) {
    MdJob *j = data;
    g_autoptr(MdVideo) video = v;
    g_autoptr(GError) local = NULL;
    if (!v || g_cancellable_is_cancelled(j->cancel)) {
        if (!v && retry_transfer(j, error))
            return;
        ended(j, FALSE, error);
        return;
    }
    g_autoptr(MdSelection) s = md_select_format(v, j->container, j->height, &local);
    if (s && s->audio && !md_audio_conversion_supported(s->container)) {
        ended(j, FALSE, "FFmpeg no dispone del codificador requerido para este formato de audio.");
        return;
    }
    if (!s || !md_directory_valid(j->directory, &local)) {
        ended(j, FALSE, local->message);
        return;
    }
    if (!j->work_directory) {
        j->work_directory = g_build_filename(j->directory, ".media-downloader-XXXXXX", NULL);
        if (!g_mkdtemp(j->work_directory)) {
            g_clear_pointer(&j->work_directory, g_free);
            ended(j, FALSE, "No se pudo crear la carpeta temporal de descarga.");
            return;
        }
    }
    g_autofree gchar *output = md_job_output_template(j);
    g_autofree gchar *retries = g_strdup_printf("%u", CLAMP(j->retries, 1u, 10u));
    g_autofree gchar *destination = g_strdup_printf("home:%s", j->work_directory);
    const gchar *argv[44];
    guint n = 0;
    argv[n++] = "yt-dlp";
    argv[n++] = "--ignore-config";
    argv[n++] = "--no-playlist";
    md_job_prepare_tracks(j, s);
    argv[n++] = "--newline";
    argv[n++] = "--progress";
    argv[n++] = "--progress-delta";
    argv[n++] = "0.2";
    argv[n++] = "--progress-template";
    argv[n++] = "download:MD_PROGRESS:{\"progress\":%(progress)j,\"format_id\":%(info.format_id)j}";
    argv[n++] = "--progress-template";
    argv[n++] = "postprocess:MD_PROGRESS:%(progress)j";
    argv[n++] = "--print";
    argv[n++] = "after_move:MD_OUTPUT:%(filepath)j";
    argv[n++] = "--no-simulate";
    argv[n++] = "--no-overwrites";
    argv[n++] = "--post-overwrites";
    argv[n++] = "--socket-timeout";
    argv[n++] = "20";
    argv[n++] = "--retries";
    argv[n++] = retries;
    argv[n++] = "--fragment-retries";
    argv[n++] = retries;
    argv[n++] = "--no-restrict-filenames";
    argv[n++] = "-f";
    argv[n++] = s->selector;
    argv[n++] = "-o";
    argv[n++] = output;
    argv[n++] = "--paths";
    argv[n++] = destination;
    if (s->audio) {
        argv[n++] = "--extract-audio";
        argv[n++] = "--audio-format";
        argv[n++] = s->container;
        argv[n++] = "--audio-quality";
        argv[n++] = "0";
    } else {
        argv[n++] = "--merge-output-format";
        argv[n++] = s->container;
        argv[n++] = "--remux-video";
        argv[n++] = s->container;
    }
    argv[n++] = "--";
    argv[n++] = j->url;
    argv[n] = NULL;
    g_clear_pointer(&j->detail, g_free);
    j->state = MD_DOWNLOADING;
    g_strfreev(j->download_argv);
    j->download_argv = g_strdupv((gchar **)argv);
    j->process = md_process_start(argv, process_line, process_done, j, &local);
    if (!j->process)
        ended(j, FALSE, local->message);
    else
        changed(j);
}
static void dependencies_ready(GObject *source, GAsyncResult *result, gpointer data) {
    (void)source;
    MdJob *j = data;
    g_autoptr(GError) error = NULL;
    gboolean ok = md_dependencies_finish(result, &error);
    if (!ok || g_cancellable_is_cancelled(j->cancel)) {
        ended(j, FALSE, error ? error->message : "Cancelado");
        return;
    }
    md_analyze_async(j->url, j->cancel, metadata_ready, j);
}
void md_manager_schedule(MdManager *m) {
    if (m->stopping)
        return;
    for (guint i = 0; i < m->jobs->len && m->active < m->limit; i++) {
        MdJob *j = g_ptr_array_index(m->jobs, i);
        if (j->state != MD_QUEUED)
            continue;
        m->active++;
        j->state = MD_ANALYZING;
        changed(j);
        md_dependencies_async(dependencies_ready, j);
    }
}
void md_job_cancel(MdJob *j) {
    if (md_state_terminal(j->state))
        return;
    g_cancellable_cancel(j->cancel);
    if (j->retry_timer) {
        g_source_remove(j->retry_timer);
        j->retry_timer = 0;
        ended(j, FALSE, "Cancelado");
        return;
    }
    if (j->state == MD_QUEUED) {
        j->state = MD_CANCELLED;
        changed(j);
    }
    if (j->process)
        md_process_cancel(j->process);
}
void md_job_retry(MdJob *j) {
    if (j->state != MD_ERROR && j->state != MD_CANCELLED)
        return;
    g_cancellable_reset(j->cancel);
    g_clear_pointer(&j->error, g_free);
    g_clear_pointer(&j->detail, g_free);
    g_clear_pointer(&j->output_path, g_free);
    j->processing_retries = 0;
    j->transfer_retries = 0;
    j->processing_started = FALSE;
    j->fraction = -1;
    g_hash_table_remove_all(j->track_progress);
    j->expected_tracks = 1;
    j->downloaded_bytes = 0;
    j->total_bytes = -1;
    j->total_estimated = FALSE;
    j->state = MD_QUEUED;
    changed(j);
    md_manager_schedule(j->manager);
}
gboolean md_manager_remove(MdManager *m, MdJob *j) {
    return md_state_terminal(j->state) && g_ptr_array_remove(m->jobs, j);
}
void md_manager_cancel_all(MdManager *m) {
    gboolean stopping = m->stopping;
    m->stopping = TRUE;
    for (guint i = 0; i < m->jobs->len; i++)
        md_job_cancel(g_ptr_array_index(m->jobs, i));
    m->stopping = stopping;
}
void md_manager_stop(MdManager *m) {
    m->stopping = TRUE;
    for (guint i = 0; i < m->jobs->len; i++)
        md_job_cancel(g_ptr_array_index(m->jobs, i));
}
void md_manager_free(MdManager *m) {
    g_assert(m->active == 0);
    g_ptr_array_unref(m->jobs);
    g_free(m);
}

GPtrArray *md_manager_display_order(MdManager *m) {
    GPtrArray *order = g_ptr_array_new();
    gboolean playlist_pending = FALSE;
    for (guint i = 0; i < m->jobs->len; i++) {
        MdJob *j = g_ptr_array_index(m->jobs, i);
        if (j->from_playlist && !md_state_terminal(j->state))
            playlist_pending = TRUE;
    }
    for (guint pass = 0; pass < (playlist_pending ? 1u : 2u); pass++) {
        for (guint i = 0; i < m->jobs->len; i++) {
            MdJob *j = g_ptr_array_index(m->jobs, i);
            gboolean active = j->state != MD_QUEUED && !md_state_terminal(j->state);
            if (playlist_pending || active == (pass == 0))
                g_ptr_array_add(order, j);
        }
    }
    return order;
}
