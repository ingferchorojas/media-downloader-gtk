#include "updates.h"
#include "engine.h"
#include "video-info.h"
#include <libsoup/soup.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <unistd.h>
#include "config.h"
void md_release_free(MdRelease *r) {
    if (r) {
        g_free(r->version);
        g_free(r->page_url);
        g_free(r->download_url);
        g_free(r->checksum_url);
        g_free(r->digest);
        g_free(r);
    }
}
void md_updates_free(MdUpdates *u) {
    if (u) {
        g_free(u->installed_engine);
        g_free(u->engine_error);
        g_free(u->app_error);
        md_release_free(u->engine);
        md_release_free(u->app);
        g_free(u);
    }
}
static gboolean version_parts(const gchar *version, guint64 parts[3]) {
    if (!version)
        return FALSE;
    if (*version == 'v')
        version++;
    for (guint i = 0; i < 3; i++) {
        if (!g_ascii_isdigit(*version))
            return FALSE;
        gchar *end = NULL;
        errno = 0;
        parts[i] = g_ascii_strtoull(version, &end, 10);
        if (errno || parts[i] > G_MAXUINT)
            return FALSE;
        if (i < 2) {
            if (*end != '.')
                return FALSE;
            version = end + 1;
        } else if (*end)
            return FALSE;
    }
    return TRUE;
}
gboolean md_version_newer(const gchar *installed, const gchar *available) {
    guint64 current[3], latest[3];
    if (!version_parts(available, latest))
        return FALSE;
    if (!installed)
        return TRUE;
    if (!version_parts(installed, current))
        return FALSE;
    for (guint i = 0; i < 3; i++) {
        if (latest[i] != current[i])
            return latest[i] > current[i];
    }
    return FALSE;
}
static gboolean json_true(JsonObject *obj, const gchar *key) {
    JsonNode *n = json_object_get_member(obj, key);
    return n && JSON_NODE_HOLDS_VALUE(n) && json_node_get_value_type(n) == G_TYPE_BOOLEAN &&
           json_node_get_boolean(n);
}
static gboolean digest_valid(const gchar *digest) {
    if (!digest || strlen(digest) != 64)
        return FALSE;
    for (guint i = 0; i < 64; i++)
        if (!g_ascii_isxdigit(digest[i]))
            return FALSE;
    return TRUE;
}
MdRelease *md_release_parse(const gchar *data, const gchar *repository, gboolean engine,
                            GError **error) {
    g_autoptr(JsonParser) parser = json_parser_new();
    if (!json_parser_load_from_data(parser, data, -1, error))
        return NULL;
    g_autofree gchar *expected = NULL;
    JsonNode *root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root))
        goto invalid;
    JsonObject *obj = json_node_get_object(root);
    const gchar *tag = md_json_string(obj, "tag_name"), *page = md_json_string(obj, "html_url");
    guint64 parts[3];
    if (!version_parts(tag, parts) || json_true(obj, "draft") || json_true(obj, "prerelease"))
        goto invalid;
    expected = g_strdup_printf("https://github.com/%s/releases/tag/%s", repository, tag);
    if (g_strcmp0(page, expected) != 0)
        goto invalid;
    MdRelease *r = g_new0(MdRelease, 1);
    r->version = g_strdup(tag[0] == 'v' ? tag + 1 : tag);
    r->page_url = g_strdup(page);
    if (engine) {
        JsonNode *assets = json_object_get_member(obj, "assets");
        if (assets && JSON_NODE_HOLDS_ARRAY(assets)) {
            JsonArray *array = json_node_get_array(assets);
            for (guint i = 0; i < json_array_get_length(array); i++) {
                JsonNode *item = json_array_get_element(array, i);
                if (!JSON_NODE_HOLDS_OBJECT(item))
                    continue;
                JsonObject *asset = json_node_get_object(item);
                const gchar *name = md_json_string(asset, "name"),
                            *url = md_json_string(asset, "browser_download_url");
                if (g_strcmp0(name, "yt-dlp") != 0 && g_strcmp0(name, "SHA2-256SUMS") != 0)
                    continue;
                g_autofree gchar *asset_url = g_strdup_printf(
                    "https://github.com/%s/releases/download/%s/%s", repository, tag, name);
                if (g_strcmp0(url, asset_url) != 0)
                    continue;
                if (g_str_equal(name, "yt-dlp")) {
                    g_free(r->download_url);
                    r->download_url = g_strdup(url);
                    const gchar *digest = md_json_string(asset, "digest");
                    if (digest && g_str_has_prefix(digest, "sha256:") && digest_valid(digest + 7)) {
                        g_free(r->digest);
                        r->digest = g_ascii_strdown(digest + 7, -1);
                    }
                } else {
                    g_free(r->checksum_url);
                    r->checksum_url = g_strdup(url);
                }
            }
        }
        if (!r->download_url || (!r->digest && !r->checksum_url)) {
            md_release_free(r);
            goto invalid;
        }
    }
    return r;
invalid:
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "GitHub devolvió información de versión no válida o incompleta.");
    return NULL;
}
/* HTTP and file operations run in a GTask worker. Bound both time and memory. */
static GBytes *request_bytes(const gchar *url, gsize limit, GCancellable *cancel, guint *status,
                             GError **error) {
    g_autoptr(SoupSession) session = soup_session_new_with_options(
        "timeout", 20u, "user-agent", "MediaDownloaderGTK/" MD_APP_VERSION, NULL);
    g_autoptr(SoupMessage) msg = soup_message_new("GET", url);
    if (!msg) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Dirección de actualización no válida.");
        return NULL;
    }
    soup_message_headers_append(soup_message_get_request_headers(msg), "Accept",
                                "application/vnd.github+json");
    g_autoptr(GInputStream) stream = soup_session_send(session, msg, cancel, error);
    if (!stream)
        return NULL;
    guint code = soup_message_get_status(msg);
    if (status)
        *status = code;
    if (code != SOUP_STATUS_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "No se pudo consultar GitHub (HTTP %u). Inténtalo más tarde.", code);
        return NULL;
    }
    g_autoptr(GByteArray) bytes = g_byte_array_new();
    guint8 buffer[16384];
    for (;;) {
        gssize n = g_input_stream_read(stream, buffer, sizeof buffer, cancel, error);
        if (n < 0)
            return NULL;
        if (!n)
            break;
        if ((gsize)n > limit - bytes->len) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                                "La respuesta de actualización supera el tamaño permitido.");
            return NULL;
        }
        g_byte_array_append(bytes, buffer, (guint)n);
    }
    return g_byte_array_free_to_bytes(g_steal_pointer(&bytes));
}
static MdRelease *request_release(const gchar *repo, gboolean engine, GCancellable *cancel,
                                  gboolean *unpublished, GError **error) {
    g_autofree gchar *url =
        g_strdup_printf("https://api.github.com/repos/%s/releases/latest", repo);
    guint status = 0;
    g_autoptr(GBytes) bytes = request_bytes(url, 2 * 1024 * 1024, cancel, &status, error);
    if (status == 404 && unpublished) {
        *unpublished = TRUE;
        g_clear_error(error);
        return NULL;
    }
    if (!bytes)
        return NULL;
    gsize size;
    const gchar *data = g_bytes_get_data(bytes, &size);
    g_autofree gchar *json = g_strndup(data, size);
    return md_release_parse(json, repo, engine, error);
}
static void force_exit(GCancellable *cancel, gpointer data) {
    (void)cancel;
    g_subprocess_force_exit(data);
}
static gchar *executable_version(const gchar *path, GCancellable *cancel, GError **error) {
    g_autoptr(GSubprocess) child =
        g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, error,
                         path, "--version", NULL);
    if (!child)
        return NULL;
    gulong handler =
        cancel ? g_cancellable_connect(cancel, G_CALLBACK(force_exit), child, NULL) : 0;
    gchar *out = NULL;
    gboolean ok = g_subprocess_communicate_utf8(child, NULL, NULL, &out, NULL, error);
    if (handler)
        g_cancellable_disconnect(cancel, handler);
    if (cancel && g_cancellable_is_cancelled(cancel)) {
        g_free(out);
        if (error && *error)
            g_clear_error(error);
        g_cancellable_set_error_if_cancelled(cancel, error);
        return NULL;
    }
    if (!ok || !g_subprocess_get_successful(child)) {
        g_free(out);
        if (ok)
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                                "No se pudo comprobar la versión de yt-dlp.");
        return NULL;
    }
    return g_strstrip(out);
}
static void check_worker(GTask *task, gpointer source, gpointer data, GCancellable *cancel) {
    (void)source;
    (void)data;
    MdUpdates *u = g_new0(MdUpdates, 1);
    g_autofree gchar *engine = md_engine_path();
    u->installed_engine = executable_version(engine, cancel, NULL);
    g_autoptr(GError) error = NULL;
    u->engine = request_release(MD_ENGINE_REPOSITORY, TRUE, cancel, NULL, &error);
    if (error) {
        u->engine_error = g_strdup(error->message);
        g_clear_error(&error);
    }
    if (!g_cancellable_is_cancelled(cancel)) {
        u->app = request_release(MD_APP_REPOSITORY, FALSE, cancel, &u->app_unpublished, &error);
        if (error)
            u->app_error = g_strdup(error->message);
    }
    g_task_return_pointer(task, u, (GDestroyNotify)md_updates_free);
}
void md_updates_check_async(GCancellable *cancel, GAsyncReadyCallback callback, gpointer data) {
    GTask *task = g_task_new(NULL, cancel, callback, data);
    g_task_run_in_thread(task, check_worker);
    g_object_unref(task);
}
MdUpdates *md_updates_check_finish(GAsyncResult *result, GError **error) {
    return g_task_propagate_pointer(G_TASK(result), error);
}
gboolean md_checksum_matches(GBytes *bytes, const gchar *digest) {
    if (!bytes || !digest_valid(digest))
        return FALSE;
    g_autofree gchar *actual = g_compute_checksum_for_bytes(G_CHECKSUM_SHA256, bytes);
    return g_ascii_strcasecmp(actual, digest) == 0;
}
static gchar *checksum_from_list(GBytes *bytes) {
    gsize size;
    const gchar *data = g_bytes_get_data(bytes, &size);
    g_autofree gchar *text = g_strndup(data, size);
    g_auto(GStrv) lines = g_strsplit(text, "\n", -1);
    for (guint i = 0; lines[i]; i++) {
        gchar *line = lines[i];
        if (strlen(line) < 66)
            continue;
        g_autofree gchar *digest = g_strndup(line, 64);
        if (!digest_valid(digest) || !g_ascii_isspace(line[64]))
            continue;
        const gchar *name = line + 64;
        while (g_ascii_isspace(*name))
            name++;
        if (*name == '*')
            name++;
        if (g_strcmp0(g_strstrip((gchar *)name), "yt-dlp") == 0)
            return g_ascii_strdown(digest, -1);
    }
    return NULL;
}
static void update_worker(GTask *task, gpointer source, gpointer data, GCancellable *cancel) {
    (void)source;
    MdRelease *release = data;
    g_autoptr(GError) error = NULL;
    g_autoptr(GBytes) bytes =
        request_bytes(release->download_url, 32 * 1024 * 1024, cancel, NULL, &error);
    if (!bytes) {
        g_task_return_error(task, g_steal_pointer(&error));
        return;
    }
    g_autofree gchar *digest = g_strdup(release->digest);
    if (!digest) {
        g_autoptr(GBytes) sums =
            request_bytes(release->checksum_url, 1024 * 1024, cancel, NULL, &error);
        if (!sums) {
            g_task_return_error(task, g_steal_pointer(&error));
            return;
        }
        digest = checksum_from_list(sums);
    }
    gchar *version = md_engine_install(bytes, digest, release->version, cancel, &error);
    if (!version)
        g_task_return_error(task, g_steal_pointer(&error));
    else
        g_task_return_pointer(task, version, g_free);
}
gchar *md_engine_install(GBytes *bytes, const gchar *digest, const gchar *expected,
                         GCancellable *cancel, GError **error) {
    if (g_cancellable_set_error_if_cancelled(cancel, error))
        return NULL;
    if (!md_checksum_matches(bytes, digest)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "La verificación SHA-256 falló. La versión instalada no se modificó.");
        return NULL;
    }
    guint64 parts[3];
    if (!version_parts(expected, parts)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "La versión anunciada no es válida.");
        return NULL;
    }
    g_autofree gchar *path = md_engine_managed_path(), *dir = g_path_get_dirname(path);
    if (g_mkdir_with_parents(dir, 0700) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "No se pudo crear la carpeta de actualización: %s", g_strerror(errno));
        return NULL;
    }
    g_autofree gchar *staging = g_strdup_printf("%s.XXXXXX", path);
    gint fd = g_mkstemp(staging);
    if (fd < 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "No se pudo preparar la actualización: %s", g_strerror(errno));
        return NULL;
    }
    close(fd);
    gsize length;
    const gchar *contents = g_bytes_get_data(bytes, &length);
    gboolean ok = g_file_set_contents_full(
        staging, contents, (gssize)length,
        G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, error);
    if (ok && g_chmod(staging, 0700) != 0) {
        ok = FALSE;
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                            "No se pudo habilitar el ejecutable actualizado.");
    }
    g_autofree gchar *version = ok ? executable_version(staging, cancel, error) : NULL;
    if (ok && !version)
        ok = FALSE;
    if (ok && g_strcmp0(version, expected) != 0) {
        ok = FALSE;
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "La versión descargada no coincide con la anunciada por GitHub.");
    }
    if (ok && g_cancellable_set_error_if_cancelled(cancel, error))
        ok = FALSE;
    if (ok && g_rename(staging, path) != 0) {
        ok = FALSE;
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "No se pudo instalar la actualización: %s", g_strerror(errno));
    }
    if (!ok) {
        g_unlink(staging);
        return NULL;
    }
    return g_steal_pointer(&version);
}

void md_engine_update_async(const MdRelease *r, GCancellable *cancel, GAsyncReadyCallback callback,
                            gpointer data) {
    GTask *task = g_task_new(NULL, cancel, callback, data);
    MdRelease *copy = g_new0(MdRelease, 1);
    copy->version = g_strdup(r->version);
    copy->download_url = g_strdup(r->download_url);
    copy->checksum_url = g_strdup(r->checksum_url);
    copy->digest = g_strdup(r->digest);
    g_task_set_task_data(task, copy, (GDestroyNotify)md_release_free);
    g_task_run_in_thread(task, update_worker);
    g_object_unref(task);
}
gchar *md_engine_update_finish(GAsyncResult *result, GError **error) {
    return g_task_propagate_pointer(G_TASK(result), error);
}
