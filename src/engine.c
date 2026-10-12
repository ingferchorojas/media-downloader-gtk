#include "engine.h"
#include <unistd.h>
static GMutex runtime_lock;
static gchar *runtime;
gchar *md_engine_managed_path(void) {
    return g_build_filename(g_get_user_data_dir(), "media-downloader-gtk", "bin", "yt-dlp", NULL);
}
gchar *md_engine_path(void) {
    gchar *managed = md_engine_managed_path();
    if (g_file_test(managed, G_FILE_TEST_IS_REGULAR) && access(managed, X_OK) == 0)
        return managed;
    g_free(managed);
    gchar *system = g_find_program_in_path("yt-dlp");
    return system ? system : g_strdup("yt-dlp");
}
gchar **md_engine_argv(const gchar *const *arguments) {
    GPtrArray *args = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(args, md_engine_path());
    g_mutex_lock(&runtime_lock);
    if (runtime) {
        g_ptr_array_add(args, g_strdup("--js-runtimes"));
        g_ptr_array_add(args, g_strdup(runtime));
    }
    g_mutex_unlock(&runtime_lock);
    for (guint i = 1; arguments[i]; i++)
        g_ptr_array_add(args, g_strdup(arguments[i]));
    g_ptr_array_add(args, NULL);
    return (gchar **)g_ptr_array_free(args, FALSE);
}
static gchar *program_output(const gchar *path, const gchar *arg) {
    g_autoptr(GSubprocess) child = g_subprocess_new(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, NULL, path, arg, NULL);
    gchar *out = NULL;
    if (!child || !g_subprocess_communicate_utf8(child, NULL, NULL, &out, NULL, NULL) ||
        !g_subprocess_get_successful(child)) {
        g_free(out);
        return NULL;
    }
    return out;
}
void md_engine_probe_runtime(const gchar *executable) {
    gchar *selected = NULL;
    g_autofree gchar *help = program_output(executable, "--help");
    if (help && strstr(help, "--js-runtimes")) {
        g_autofree gchar *deno = g_find_program_in_path("deno"),
                         *node = g_find_program_in_path("node");
        g_autofree gchar *deno_version = deno ? program_output(deno, "--version") : NULL;
        g_autofree gchar *node_version = node ? program_output(node, "--version") : NULL;
        if (deno_version) {
            gchar *start = strstr(deno_version, "deno ");
            if (start) {
                gchar *tail = NULL;
                guint64 major = g_ascii_strtoull(start + 5, &tail, 10);
                guint64 minor = tail && *tail == '.' ? g_ascii_strtoull(tail + 1, NULL, 10) : 0;
                if (major > 2 || (major == 2 && minor >= 3))
                    selected = g_strdup_printf("deno:%s", deno);
            }
        }
        if (!selected && node_version && node_version[0] == 'v' &&
            g_ascii_strtoull(node_version + 1, NULL, 10) >= 22)
            selected = g_strdup_printf("node:%s", node);
    }
    g_mutex_lock(&runtime_lock);
    g_free(runtime);
    runtime = selected;
    g_mutex_unlock(&runtime_lock);
}
