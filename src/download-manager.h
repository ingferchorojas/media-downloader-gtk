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
    gchar *url, *title, *directory, *container, *error, *detail, *work_directory, *output_path;
    gint height;
    MdState state;
    gdouble fraction;
    GHashTable *track_progress;
    guint expected_tracks, retries, playlist_number, processing_retries, transfer_retries, retry_timer;
    gboolean retry_analysis;
    gchar **download_argv;
    gboolean processing_started;
    gdouble downloaded_bytes, total_bytes;
    gboolean total_estimated;
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
void md_manager_cancel_all(MdManager *manager);
void md_manager_stop(MdManager *manager);
void md_manager_free(MdManager *manager);
const gchar *md_state_name(MdState state);
gboolean md_state_terminal(MdState state);

gboolean md_retryable_error(const gchar *error);
guint md_retry_delay(guint attempt);
gchar *md_job_output_template(const MdJob *job);
void md_job_prepare_tracks(MdJob *job, const MdSelection *selection);
void md_job_apply_progress(MdJob *job, const MdProgress *progress);

/* Borrowed jobs in display order; does not change scheduling order. */
GPtrArray *md_manager_display_order(MdManager *manager);
