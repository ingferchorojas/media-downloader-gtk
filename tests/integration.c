#include "download-manager.h"
#include "utils.h"
#include <glib/gstdio.h>
static GMainLoop *loop;
static guint updates, processed, finished, expected = 1, max_active;
static const gchar *mode;
static void changed(MdJob *j, gpointer data) {
    (void)data;
    gdouble *previous = g_object_get_data(G_OBJECT(j->cancel), "previous-fraction");
    if (!previous) {
        previous = g_new(gdouble, 1);
        *previous = -1;
        g_object_set_data_full(G_OBJECT(j->cancel), "previous-fraction", previous, g_free);
    }
    g_assert_cmpfloat(j->fraction, >=, *previous);
    if (j->state != MD_COMPLETED)
        g_assert_cmpfloat(j->fraction, <, 1);
    *previous = j->fraction;
    max_active = MAX(max_active, j->manager->active);
    g_assert_cmpuint(j->manager->active, <=, j->manager->limit);
    if (j->state == MD_DOWNLOADING && j->detail) {
        updates++;
        if (g_strcmp0(mode, "cancel") == 0)
            md_job_cancel(j);
    }
    if (j->state == MD_PROCESSING || j->state == MD_MERGING || j->state == MD_CONVERTING)
        processed++;
    if (md_state_terminal(j->state)) {
        g_print("%s: %s\n", md_state_name(j->state), j->error ? j->error : j->title);
        finished++;
        if (finished == expected)
            g_main_loop_quit(loop);
    }
}
static gboolean timeout(gpointer data) {
    md_manager_stop(data);
    g_error("Integration timeout");
    return G_SOURCE_REMOVE;
}
int main(int argc, char **argv) {
    if (argc < 4)
        return 2;
    mode = argc > 4 ? argv[4] : "download";
    if (g_str_equal(mode, "queue"))
        expected = 4;
    loop = g_main_loop_new(NULL, FALSE);
    MdManager *m = md_manager_new(changed, NULL);
    for (guint i = 0; i < expected; i++) {
        g_autofree gchar *dir =
            expected > 1 ? g_strdup_printf("%s/job-%u", argv[2], i) : g_strdup(argv[2]);
        g_assert_cmpint(g_mkdir_with_parents(dir, 0700), ==, 0);
        md_manager_add(m, argv[1], "Generated test media", dir, argv[3], 0);
    }
    guint timer = g_timeout_add_seconds(60, timeout, m);
    md_manager_schedule(m);
    g_main_loop_run(loop);
    g_source_remove(timer);
    gboolean ok = TRUE;
    for (guint i = 0; i < expected; i++) {
        MdJob *j = g_ptr_array_index(m->jobs, i);
        MdState state = g_str_equal(mode, "cancel")  ? MD_CANCELLED
                        : g_str_equal(mode, "error") ? MD_ERROR
                                                     : MD_COMPLETED;
        if (j->state != state)
            ok = FALSE;
    }
    if (expected > 1)
        g_assert_cmpuint(max_active, ==, 2);
    g_print("Progress updates: %u; processing updates: %u; peak concurrent jobs: %u\n", updates,
            processed, max_active);
    md_manager_free(m);
    g_main_loop_unref(loop);
    return ok ? 0 : 1;
}
