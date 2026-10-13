#pragma once
#include "video-info.h"
typedef enum { MD_DIRECT, MD_MERGE, MD_CONVERT } MdProcessing;
typedef struct {
    gchar *selector, *container, *audio_language;
    gboolean audio;
    guint tracks;
    gchar *track_ids[2];
    gdouble track_sizes[2];
    gboolean track_estimated[2];
    MdProcessing processing;
} MdSelection;
const gchar *md_processing_name(MdProcessing processing);
MdSelection *md_select_format(MdVideo *video, const gchar *container, gint height, GError **error);
void md_selection_free(MdSelection *selection);
GArray *md_resolutions(MdVideo *video);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(MdSelection, md_selection_free)
