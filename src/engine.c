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
static gboolean supported_node(const gchar *path) {
    g_autofree gchar *version = program_output(path, "--version");
    return version && version[0] == 'v' && g_ascii_strtoull(version + 1, NULL, 10) >= 22;
}
static gchar *find_node(void) {
    g_autofree gchar *node = g_find_program_in_path("node");
    if (node && supported_node(node))
        return g_steal_pointer(&node);
    const gchar *configured = g_getenv("NVM_DIR");
    g_autofree gchar *root = configured && *configured
                                 ? g_strdup(configured)
                                 : g_build_filename(g_get_home_dir(), ".nvm", NULL);
    g_autofree gchar *versions = g_build_filename(root, "versions", "node", NULL);
    GDir *dir = g_dir_open(versions, 0, NULL);
    if (!dir)
        return NULL;
    const gchar *name;
    guint checked = 0;
    gchar *found = NULL;
    while (checked++ < 64 && (name = g_dir_read_name(dir))) {
        if (name[0] != 'v' || g_ascii_strtoull(name + 1, NULL, 10) < 22)
            continue;
        g_autofree gchar *candidate = g_build_filename(versions, name, "bin", "node", NULL);
        if (g_file_test(candidate, G_FILE_TEST_IS_REGULAR) && access(candidate, X_OK) == 0 &&
            supported_node(candidate)) {
            found = g_steal_pointer(&candidate);
            break;
        }
    }
    g_dir_close(dir);
    return found;
}
void md_engine_probe_runtime(const gchar *executable) {
    gchar *selected = NULL;
    g_autofree gchar *help = program_output(executable, "--help");
    if (help && strstr(help, "--js-runtimes")) {
        g_autofree gchar *deno = g_find_program_in_path("deno");
        g_autofree gchar *deno_version = deno ? program_output(deno, "--version") : NULL;
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
        if (!selected) {
            g_autofree gchar *node = find_node();
            if (node)
                selected = g_strdup_printf("node:%s", node);
        }
    }
    g_mutex_lock(&runtime_lock);
    g_free(runtime);
    runtime = selected;
    g_mutex_unlock(&runtime_lock);
}
