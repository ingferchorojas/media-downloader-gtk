#pragma once
#include "downloader.h"
#include "format-selector.h"
typedef enum {
    MD_QUEUED,
    MD_ANALYZING,
    MD_DOWNLOADING,
    MD_PROCESSING,
    MD_MERGING,
    MD_CONVERTING,
    MD_COMPLETED,
    MD_CANCELLED,
    MD_ERROR
} MdState;
typedef struct _MdManager MdManager;
typedef struct {
    MdManager *manager;
    gchar *url, *title, *directory, *container, *error, *detail;
    gint height;
    MdState state;
    gdouble fraction;
    GHashTable *track_progress;
    guint expected_tracks;
    gboolean from_playlist;
    MdProcess *process;
    GCancellable *cancel;
    gpointer ui;
} MdJob;
struct _MdManager {
    GPtrArray *jobs;
    guint limit, active;
    gboolean stopping;
    void (*changed)(MdJob *, gpointer);
    gpointer data;
};
MdManager *md_manager_new(void (*changed)(MdJob *, gpointer), gpointer data);
MdJob *md_manager_add(MdManager *manager, const gchar *url, const gchar *title,
                      const gchar *directory, const gchar *container, gint height);
void md_manager_schedule(MdManager *manager);
void md_job_cancel(MdJob *job);
void md_job_retry(MdJob *job);
gboolean md_manager_remove(MdManager *manager, MdJob *job);
void md_manager_stop(MdManager *manager);
void md_manager_free(MdManager *manager);
const gchar *md_state_name(MdState state);
gboolean md_state_terminal(MdState state);

void md_job_apply_progress(MdJob *job, const MdProgress *progress);

/* Borrowed jobs in display order; does not change scheduling order. */
GPtrArray *md_manager_display_order(MdManager *manager);
