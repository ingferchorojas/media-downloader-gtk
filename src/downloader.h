#pragma once
#include <gio/gio.h>
#include "video-info.h"
typedef struct _MdProcess MdProcess;
typedef void (*MdLineFunc)(const gchar *line, gpointer data);
typedef void (*MdDoneFunc)(gboolean success, const gchar *error, gpointer data);
MdProcess *md_process_start(const gchar *const *argv, MdLineFunc line, MdDoneFunc done,
                            gpointer data, GError **error);
void md_process_cancel(MdProcess *process);
void md_process_free(MdProcess *process);
typedef void (*MdAnalysisFunc)(MdVideo *video, const gchar *error, gpointer data);
void md_analyze_async(const gchar *url, GCancellable *cancel, MdAnalysisFunc callback,
                      gpointer data);
void md_dependencies_async(GAsyncReadyCallback callback, gpointer data);
gboolean md_dependencies_finish(GAsyncResult *result, GError **error);
gboolean md_audio_conversion_supported(const gchar *container);
typedef struct {
    gdouble downloaded, total, speed, eta;
    gboolean estimated;
    gchar *status, *stage, *filename, *format_id;
    gint fragment, fragments;
} MdProgress;
gboolean md_progress_parse(const gchar *line, MdProgress *progress);
void md_progress_clear(MdProgress *progress);

gboolean md_output_matches(const gchar *json, const gchar *container, GError **error);
