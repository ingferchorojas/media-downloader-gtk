#pragma once
#include <gio/gio.h>
gchar *md_engine_managed_path(void);
gchar *md_engine_path(void);
gchar **md_engine_argv(const gchar *const *arguments);
/* Probe off the GTK thread. Runtime selection is shared under a mutex. */
void md_engine_probe_runtime(const gchar *executable);
