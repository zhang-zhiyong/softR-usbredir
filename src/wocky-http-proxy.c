 /* wocky-http-proxy.c: Source for WockyHttpProxy
 *
 * Copyright (C) 2010 Collabora, Ltd.
 * Copyright (C) 2014 Red Hat, Inc.
 * @author Nicolas Dufresne <nicolas.dufresne@collabora.co.uk>
 * @author Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"

#include "wocky-http-proxy.h"

#include <string.h>
#include <stdlib.h>


struct _WockyHttpProxy
{
  GObject parent;
};

struct _WockyHttpProxyClass
{
  GObjectClass parent_class;
};

static void wocky_http_proxy_iface_init (GProxyInterface *proxy_iface);

#define wocky_http_proxy_get_type _wocky_http_proxy_get_type
G_DEFINE_TYPE_WITH_CODE (WockyHttpProxy, wocky_http_proxy, G_TYPE_OBJECT,
  G_IMPLEMENT_INTERFACE (G_TYPE_PROXY,
    wocky_http_proxy_iface_init)
  g_io_extension_point_set_required_type (
    g_io_extension_point_register (G_PROXY_EXTENSION_POINT_NAME),
    G_TYPE_PROXY);
  g_io_extension_point_implement (G_PROXY_EXTENSION_POINT_NAME,
    g_define_type_id, "http", 0))

static void
wocky_http_proxy_init (WockyHttpProxy *proxy)
{
}

#define HTTP_END_MARKER "\r\n\r\n"

static gchar *
create_request (GProxyAddress *proxy_address, gboolean *has_cred)
{
  const gchar *hostname;
  gint port;
  const gchar *username;
  const gchar *password;
  GString *request;
  gchar *ascii_hostname;

  if (has_cred)
    *has_cred = FALSE;

  hostname = g_proxy_address_get_destination_hostname (proxy_address);
  port = g_proxy_address_get_destination_port (proxy_address);
  username = g_proxy_address_get_username (proxy_address);
  password = g_proxy_address_get_password (proxy_address);

  request = g_string_new (NULL);

  ascii_hostname = g_hostname_to_ascii (hostname);
  g_string_append_printf (request,
      "CONNECT %s:%i HTTP/1.0\r\n"
        "Host: %s:%i\r\n"
        "Proxy-Connection: keep-alive\r\n"
        "User-Agent: GLib/%i.%i\r\n",
      ascii_hostname, port,
      ascii_hostname, port,
      GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION);
  g_free (ascii_hostname);

  if (username != NULL && password != NULL)
    {
      gchar *cred;
      gchar *base64_cred;

      if (has_cred)
        *has_cred = TRUE;

      cred = g_strdup_printf ("%s:%s", username, password);
      base64_cred = g_base64_encode ((guchar *) cred, strlen (cred));
      g_free (cred);
      g_string_append_printf (request,
          "Proxy-Authorization: Basic %s\r\n",
          base64_cred);
      g_free (base64_cred);
    }

  g_string_append (request, "\r\n");

  return g_string_free (request, FALSE);
}

static gboolean
check_reply (const gchar *buffer, gboolean has_cred, GError **error)
{
  gint err_code;
  const gchar *ptr = buffer + 7;

  if (strncmp (buffer, "HTTP/1.", 7) != 0
      || (*ptr != '0' && *ptr != '1'))
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_PROXY_FAILED,
          "Bad HTTP proxy reply");
      return FALSE;
    }

  ptr++;
  while (*ptr == ' ') ptr++;

  err_code = atoi (ptr);

  if (err_code < 200 || err_code >= 300)
    {
      const gchar *msg_start;
      gchar *msg;

      while (g_ascii_isdigit (*ptr))
        ptr++;

      while (*ptr == ' ')
        ptr++;

      msg_start = ptr;

      ptr = strchr (msg_start, '\r');

      if (ptr == NULL)
        ptr = strchr (msg_start, '\0');

      msg = g_strndup (msg_start, ptr - msg_start);

      if (err_code == 407)
        {
          if (has_cred)
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_PROXY_AUTH_FAILED,
                "HTTP proxy authentication failed");
          else
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_PROXY_NEED_AUTH,
                "HTTP proxy authentication required");
        }
      else if (msg[0] == '\0')
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_PROXY_FAILED,
            "Connection failed due to broken HTTP reply");
      else
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_PROXY_FAILED,
            "HTTP proxy connection failed: %i %s",
            err_code, msg);

      g_free (msg);
      return FALSE;
    }

  return TRUE;
}

static GIOStream *
wocky_http_proxy_connect (GProxy *proxy,
    GIOStream *io_stream,
    GProxyAddress *proxy_address,
    GCancellable *cancellable,
    GError **error)
{
  GInputStream *in;
  GOutputStream *out;
  GDataInputStream *data_in = NULL;
  gchar *buffer = NULL;
  gboolean has_cred;
  GIOStream *tlsconn = NULL;

  if (WOCKY_IS_HTTPS_PROXY (proxy))
    {
      tlsconn = g_tls_client_connection_new (io_stream,
                                             G_SOCKET_CONNECTABLE(proxy_address),
                                             error);
      if (!tlsconn)
          goto error;

      GTlsCertificateFlags tls_validation_flags = G_TLS_CERTIFICATE_VALIDATE_ALL;
#ifdef DEBUG
      tls_validation_flags &= ~(G_TLS_CERTIFICATE_UNKNOWN_CA | G_TLS_CERTIFICATE_BAD_IDENTITY);
#endif
      g_tls_client_connection_set_validation_flags (G_TLS_CLIENT_CONNECTION (tlsconn),
                                                    tls_validation_flags);
      if (!g_tls_connection_handshake (G_TLS_CONNECTION (tlsconn), cancellable, error))
          goto error;

      io_stream = tlsconn;
    }

  in = g_io_stream_get_input_stream (io_stream);
  out = g_io_stream_get_output_stream (io_stream);

  data_in = g_data_input_stream_new (in);
  g_filter_input_stream_set_close_base_stream (G_FILTER_INPUT_STREAM (data_in),
      FALSE);

  buffer = create_request (proxy_address, &has_cred);
  if (!g_output_stream_write_all (out, buffer, strlen (buffer), NULL,
        cancellable, error))
      goto error;

  g_free (buffer);
  buffer = g_data_input_stream_read_until (data_in, HTTP_END_MARKER, NULL,
      cancellable, error);
  g_clear_object(&data_in);

  if (buffer == NULL)
    {
      if (error && (*error == NULL))
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_PROXY_FAILED,
            "HTTP proxy server closed connection unexpectedly.");
      goto error;
    }

  if (!check_reply (buffer, has_cred, error))
    goto error;

  g_free (buffer);

  g_object_ref (io_stream);
  g_clear_object (&tlsconn);

  return io_stream;

error:
  g_clear_object (&tlsconn);
  g_clear_object (&data_in);
  g_free (buffer);
  return NULL;
}


typedef struct
{
  GIOStream *io_stream;
  gchar *buffer;
  gssize length;
  gssize offset;
  GDataInputStream *data_in;
  gboolean has_cred;
} ConnectAsyncData;

static void request_write_cb (GObject *source,
    GAsyncResult *res,
    gpointer user_data);
static void reply_read_cb (GObject *source,
    GAsyncResult *res,
    gpointer user_data);

static void
free_connect_data (ConnectAsyncData *data)
{
  if (data->io_stream != NULL)
    g_object_unref (data->io_stream);

  g_free (data->buffer);

  if (data->data_in != NULL)
    g_object_unref (data->data_in);

  g_free (data);
}

static void
complete_async_from_error (GTask *task, GError *error)
{
  if (error == NULL)
    g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_PROXY_FAILED,
        "HTTP proxy server closed connection unexpectedly.");

  g_task_return_error (task, error);
  g_object_unref (task);
}

static void
do_write (GAsyncReadyCallback callback, GTask *task)
{
  GOutputStream *out;
  ConnectAsyncData *data = g_task_get_task_data (task);
  out = g_io_stream_get_output_stream (data->io_stream);
  g_output_stream_write_async (out,
      data->buffer + data->offset,
      data->length - data->offset,
      G_PRIORITY_DEFAULT, g_task_get_cancellable(task),
      callback, task);
}

static void
stream_connected (GTask *task,
                  GIOStream *io_stream)
{
  GInputStream *in;
  ConnectAsyncData *data = g_task_get_task_data (task);

  data->io_stream = g_object_ref (io_stream);
  in = g_io_stream_get_input_stream (io_stream);
  data->data_in = g_data_input_stream_new (in);
  g_filter_input_stream_set_close_base_stream (G_FILTER_INPUT_STREAM (data->data_in),
                                               FALSE);

  do_write (request_write_cb, task);
}

static void
handshake_completed (GObject *source_object,
                     GAsyncResult *res,
                     gpointer user_data)
{
  GTlsConnection *conn = G_TLS_CONNECTION (source_object);
  GTask *task = G_TASK (user_data);
  GError *error = NULL;

  if (!g_tls_connection_handshake_finish (conn, res, &error))
    {
      complete_async_from_error (task, error);
      return;
    }

  stream_connected (task, G_IO_STREAM (conn));
}

static void
wocky_http_proxy_connect_async (GProxy *proxy,
    GIOStream *io_stream,
    GProxyAddress *proxy_address,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GTask *task;
  ConnectAsyncData *data;

  task = g_task_new (proxy,
                     cancellable,
                     callback,
                     user_data);

  data = g_new0 (ConnectAsyncData, 1);

  data->buffer = create_request (proxy_address, &data->has_cred);
  data->length = strlen (data->buffer);
  data->offset = 0;

  g_task_set_task_data (task, data, (GDestroyNotify)free_connect_data);

  if (WOCKY_IS_HTTPS_PROXY (proxy))
    {
      GError *error = NULL;
      GIOStream *tlsconn;

      tlsconn = g_tls_client_connection_new (io_stream,
                                             G_SOCKET_CONNECTABLE(proxy_address),
                                             &error);
      if (!tlsconn)
        {
          complete_async_from_error (task, error);
          return;
        }

      g_return_if_fail (tlsconn != NULL);

      GTlsCertificateFlags tls_validation_flags = G_TLS_CERTIFICATE_VALIDATE_ALL;
#ifdef DEBUG
      tls_validation_flags &= ~(G_TLS_CERTIFICATE_UNKNOWN_CA | G_TLS_CERTIFICATE_BAD_IDENTITY);
#endif
      g_tls_client_connection_set_validation_flags (G_TLS_CLIENT_CONNECTION (tlsconn),
                                                    tls_validation_flags);
      g_tls_connection_handshake_async (G_TLS_CONNECTION (tlsconn),
                                        G_PRIORITY_DEFAULT, cancellable,
                                        handshake_completed, task);
    }
  else
    {
      stream_connected (task, io_stream);
    }
}

static void
request_write_cb (GObject *source,
    GAsyncResult *res,
    gpointer user_data)
{
  GError *error = NULL;
  GTask *task = G_TASK(user_data);
  ConnectAsyncData *data = g_task_get_task_data (task);
  gssize written;

  written = g_output_stream_write_finish (G_OUTPUT_STREAM (source),
      res, &error);
  if (written < 0)
    {
      complete_async_from_error (task, error);
      return;
    }

  data->offset += written;

   if (data->offset == data->length)
    {
      g_clear_pointer(&data->buffer, g_free);

      g_data_input_stream_read_until_async (data->data_in,
          HTTP_END_MARKER,
          G_PRIORITY_DEFAULT,
          g_task_get_cancellable(task),
          reply_read_cb, task);

    }
  else
    {
      do_write (request_write_cb, task);
    }
}

static void
reply_read_cb (GObject *source,
    GAsyncResult *res,
    gpointer user_data)
{
  GError *error = NULL;
  GTask *task = G_TASK(user_data);
  ConnectAsyncData *data = g_task_get_task_data (task);

  data->buffer = g_data_input_stream_read_until_finish (data->data_in,
      res, NULL, &error);

  if (data->buffer == NULL)
    {
      complete_async_from_error (task, error);
      return;
    }

  if (!check_reply (data->buffer, data->has_cred, &error))
    {
      complete_async_from_error (task, error);
      return;
    }

  g_task_return_pointer (task, data->io_stream, (GDestroyNotify) g_object_unref);
  data->io_stream = NULL;
  g_object_unref (task);
}

static GIOStream *
wocky_http_proxy_connect_finish (GProxy *proxy,
    GAsyncResult *result,
    GError **error)
{
  GTask *task = G_TASK (result);

  return g_task_propagate_pointer (task, error);
}

static gboolean
wocky_http_proxy_supports_hostname (GProxy *proxy)
{
  return TRUE;
}

static void
wocky_http_proxy_class_init (WockyHttpProxyClass *class)
{
}

static void
wocky_http_proxy_iface_init (GProxyInterface *proxy_iface)
{
  proxy_iface->connect  = wocky_http_proxy_connect;
  proxy_iface->connect_async = wocky_http_proxy_connect_async;
  proxy_iface->connect_finish = wocky_http_proxy_connect_finish;
  proxy_iface->supports_hostname = wocky_http_proxy_supports_hostname;
}

struct _WockyHttpsProxy
{
  WockyHttpProxy parent;
};

struct _WockyHttpsProxyClass
{
  WockyHttpProxyClass parent_class;
};

#define wocky_https_proxy_get_type _wocky_https_proxy_get_type
G_DEFINE_TYPE_WITH_CODE (WockyHttpsProxy, wocky_https_proxy, WOCKY_TYPE_HTTP_PROXY,
  G_IMPLEMENT_INTERFACE (G_TYPE_PROXY,
    wocky_http_proxy_iface_init)
  g_io_extension_point_set_required_type (
    g_io_extension_point_register (G_PROXY_EXTENSION_POINT_NAME),
    G_TYPE_PROXY);
  g_io_extension_point_implement (G_PROXY_EXTENSION_POINT_NAME,
    g_define_type_id, "https", 0))

static void
wocky_https_proxy_init (WockyHttpsProxy *proxy)
{
}

static void
wocky_https_proxy_class_init (WockyHttpsProxyClass *class)
{
}
