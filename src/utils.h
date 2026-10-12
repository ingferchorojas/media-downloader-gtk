#pragma once
#include <gio/gio.h>
gboolean md_url_valid(const gchar *url);
gchar *md_size(gdouble bytes);
gchar *md_duration(gdouble seconds);
gboolean md_directory_valid(const gchar *path, GError **error);

gchar *md_error_message(const gchar *detail);
