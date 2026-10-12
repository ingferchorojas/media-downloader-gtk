#include "download-manager.h"
#include "utils.h"
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
    g_free(j->url);
    g_free(j->title);
    g_free(j->directory);
    g_free(j->container);
    g_free(j->error);
    g_free(j->detail);
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
    j->cancel = g_cancellable_new();
    j->fraction = -1;
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
static void process_done(gboolean ok, const gchar *error, gpointer data) {
    MdJob *j = data;
    g_autofree gchar *detail = g_strdup(error);
    md_process_free(j->process);
    j->process = NULL;
    ended(j, ok, detail);
}
static void process_line(const gchar *line, gpointer data) {
    MdJob *j = data;
    MdProgress p;
    if (md_progress_parse(line, &p)) {
        if (p.stage) {
            j->state = g_strrstr(p.stage, "Merger") ? MD_MERGING
                       : (g_strrstr(p.stage, "ExtractAudio") || g_strrstr(p.stage, "Convert"))
                           ? MD_CONVERTING
                           : MD_PROCESSING;
            j->fraction = -1;
            g_free(j->detail);
            j->detail = g_strdup(j->state == MD_MERGING ? "Combinando las pistas descargadas…"
                                 : j->state == MD_CONVERTING ? "Convirtiendo el archivo multimedia…"
                                                             : "Preparando el archivo final…");
        } else {
            j->state = g_strcmp0(p.status, "finished") == 0 ? MD_PROCESSING : MD_DOWNLOADING;
            j->fraction =
                p.total > 0 && p.downloaded >= 0 ? CLAMP(p.downloaded / p.total, 0, 0.99) : -1;
            g_autofree gchar *down = md_size(p.downloaded), *total = md_size(p.total),
                             *speed = md_size(p.speed), *eta = md_duration(p.eta);
            g_autofree gchar *fragments =
                p.fragments > 0 ? g_strdup_printf(" · Fragmento %d/%d", p.fragment, p.fragments)
                                : g_strdup("");
            g_free(j->detail);
            j->detail = g_strdup_printf("Pista actual: %s / %s%s · %s%s · Restante: %s%s", down,
                                        p.estimated ? "≈ " : "", total, speed,
                                        p.speed >= 0 ? "/s" : "", eta, fragments);
        }
        md_progress_clear(&p);
        changed(j);
    } else if (g_str_has_prefix(line, "ERROR:")) {
        g_free(j->error);
        j->error = md_error_message(line + 6);
        changed(j);
    }
}
static void metadata_ready(MdVideo *v, const gchar *error, gpointer data) {
    MdJob *j = data;
    g_autoptr(MdVideo) video = v;
    g_autoptr(GError) local = NULL;
    if (!v || g_cancellable_is_cancelled(j->cancel)) {
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
    g_autofree gchar *output = g_strdup("%(title).160B [%(id)s].%(ext)s");
    g_autofree gchar *destination = g_strdup_printf("home:%s", j->directory);
    const gchar *argv[40];
    guint n = 0;
    argv[n++] = "yt-dlp";
    argv[n++] = "--ignore-config";
    argv[n++] = "--no-playlist";
    argv[n++] = "--newline";
    argv[n++] = "--progress";
    argv[n++] = "--progress-delta";
    argv[n++] = "0.2";
    argv[n++] = "--progress-template";
    argv[n++] = "download:MD_PROGRESS:%(progress)j";
    argv[n++] = "--progress-template";
    argv[n++] = "postprocess:MD_PROGRESS:%(progress)j";
    argv[n++] = "--no-overwrites";
    argv[n++] = "--no-post-overwrites";
    argv[n++] = "--socket-timeout";
    argv[n++] = "20";
    argv[n++] = "--retries";
    argv[n++] = "3";
    argv[n++] = "--restrict-filenames";
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
    j->state = MD_DOWNLOADING;
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
    j->fraction = -1;
    j->state = MD_QUEUED;
    changed(j);
    md_manager_schedule(j->manager);
}
gboolean md_manager_remove(MdManager *m, MdJob *j) {
    return md_state_terminal(j->state) && g_ptr_array_remove(m->jobs, j);
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
