#include "utils.h"
#include <math.h>
#include <glib/gstdio.h>
#include <unistd.h>
gboolean md_url_valid(const gchar *url) {
    if (!url || strlen(url) > 8192)
        return FALSE;
    for (const gchar *p = url; *p; p++)
        if (g_ascii_isspace(*p) || (guchar)*p < 32)
            return FALSE;
    g_autoptr(GUri) uri = g_uri_parse(url, G_URI_FLAGS_NONE, NULL);
    if (!uri)
        return FALSE;
    const gchar *scheme = g_uri_get_scheme(uri), *host = g_uri_get_host(uri);
    return scheme && host && *host && !g_uri_get_userinfo(uri) &&
           (g_ascii_strcasecmp(scheme, "https") == 0 || g_ascii_strcasecmp(scheme, "http") == 0);
}
gchar *md_size(gdouble bytes) {
    if (!isfinite(bytes) || bytes < 0)
        return g_strdup("—");
    const gchar *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    guint unit = 0;
    while (bytes >= 1024 && unit < 4) {
        bytes /= 1024;
        unit++;
    }
    return g_strdup_printf("%.1f %s", bytes, units[unit]);
}
gchar *md_duration(gdouble seconds) {
    if (!isfinite(seconds) || seconds < 0 || seconds > G_MAXINT)
        return g_strdup("—");
    guint s = (guint)seconds;
    return g_strdup_printf("%u:%02u:%02u", s / 3600, (s / 60) % 60, s % 60);
}
gboolean md_directory_valid(const gchar *path, GError **error) {
    if (!path || !g_path_is_absolute(path) || !g_file_test(path, G_FILE_TEST_IS_DIR) ||
        access(path, W_OK | X_OK)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                            "La carpeta no existe o no permite escribir archivos.");
        return FALSE;
    }
    return TRUE;
}

gchar *md_error_message(const gchar *detail) {
    const gchar *summary =
        "No se pudo completar la operación. Comprueba la URL, la conexión y las dependencias.";
    if (detail && (strstr(detail, "No space left") || strstr(detail, "Errno 28")))
        summary = "No hay espacio suficiente en la carpeta de destino.";
    else if (detail && (strstr(detail, "Permission denied") || strstr(detail, "Errno 13")))
        summary = "No hay permisos para escribir en la carpeta de destino.";
    else if (detail && (strstr(detail, "HTTP Error 404") || strstr(detail, "unavailable") ||
                        strstr(detail, "not available")))
        summary = "El contenido no está disponible o la URL no existe.";
    else if (detail && (strstr(detail, "timed out") || strstr(detail, "Network") ||
                        strstr(detail, "Temporary failure")))
        summary = "No se pudo conectar con el sitio. Comprueba la conexión e inténtalo de nuevo.";
    else if (detail && (strstr(detail, "ffmpeg") || strstr(detail, "FFmpeg")))
        summary = "Falló el procesamiento multimedia. Comprueba FFmpeg y la compatibilidad del "
                  "contenido.";
    g_autofree gchar *bounded =
        detail ? g_utf8_make_valid(detail, MIN(strlen(detail), 2000u)) : NULL;
    return bounded && *bounded ? g_strdup_printf("%s\nDetalles: %s", summary, bounded)
                               : g_strdup(summary);
}
