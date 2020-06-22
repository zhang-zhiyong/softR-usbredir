#include <glib.h>
#include <sys/stat.h>
#include "spice-client.h"
#include "spice-common.h"
#include "spice-cmdline.h"


typedef struct spice_connection spice_connection;
struct spice_connection {
    SpiceSession     *session;
    SpiceMainChannel *main;
    int              channels;
    int              disconnecting;
};

static spice_connection *connection_new(void);
static void connection_connect(spice_connection *conn);
static void connection_disconnect(spice_connection *conn);
static void connection_destroy(spice_connection *conn);
static GMainLoop     *mainloop = NULL;
static int           connections = 0;

static void main_channel_event(SpiceChannel *channel, SpiceChannelEvent event,gpointer data)
{
    const GError *error = NULL;
    spice_connection *conn = data;
    switch (event) {
    case SPICE_CHANNEL_OPENED:
        g_print("vanxum-usbredir:main channel: opened");
        break;
    case SPICE_CHANNEL_SWITCHING:
        g_print("main channel: switching host");
        break;
    case SPICE_CHANNEL_CLOSED:
        g_print("main channel: closed");
        connection_disconnect(conn);
        break;
    case SPICE_CHANNEL_ERROR_IO:
        connection_disconnect(conn);
        break;
    case SPICE_CHANNEL_ERROR_TLS:
    case SPICE_CHANNEL_ERROR_LINK:
    case SPICE_CHANNEL_ERROR_CONNECT:
        error = spice_channel_get_error(channel);
        g_print("main channel: failed to connect");
        if (error) {
            g_print("channel error: %s", error->message);
        }
        connection_disconnect(conn);
        break;
    case SPICE_CHANNEL_ERROR_AUTH:
        g_print("main channel: auth failure (wrong password?)");
            connection_disconnect(conn);
        break;
    default:
        g_print("unknown main channel event: %u", event);
        break;
    }
}

static void channel_new(SpiceSession *s, SpiceChannel *channel, gpointer data)
{
    spice_connection *conn = data;
    int id;
    g_object_get(channel, "channel-id", &id, NULL);
    conn->channels++;
    if (SPICE_IS_MAIN_CHANNEL(channel)) {
        conn->main = SPICE_MAIN_CHANNEL(channel);
        g_signal_connect(channel, "channel-event",G_CALLBACK(main_channel_event), conn);
    }
}

static void channel_destroy(SpiceSession *s, SpiceChannel *channel, gpointer data)
{
    spice_connection *conn = data;
    int id;
    g_object_get(channel, "channel-id", &id, NULL);
    if (SPICE_IS_MAIN_CHANNEL(channel)) {
        conn->main = NULL;
    }
    conn->channels--;
    if (conn->channels > 0) {
        return;
    }
    connection_destroy(conn);
}

static spice_connection *connection_new(void)
{
    spice_connection *conn;
    conn = g_new0(spice_connection, 1);
    conn->session = spice_session_new();
    g_signal_connect(conn->session, "channel-new",G_CALLBACK(channel_new), conn);
    g_signal_connect(conn->session, "channel-destroy",G_CALLBACK(channel_destroy), conn);
    connections++;
    return conn;
}

static void connection_connect(spice_connection *conn)
{
    conn->disconnecting = false;
    spice_session_connect(conn->session);
}

static void connection_disconnect(spice_connection *conn)
{
    if (conn->disconnecting)
        return;
    conn->disconnecting = true;
    spice_session_disconnect(conn->session);
}

static void connection_destroy(spice_connection *conn)
{
    g_object_unref(conn->session);
    free(conn);
    connections--;
    if (connections > 0) {
        return;
    }
    g_main_loop_quit(mainloop);
}

int main(int argc, char *argv[])
{

    GError *error = NULL;
    GOptionContext *context;
    spice_connection *conn;
	
    context = g_option_context_new("-VANXUM test application");
    g_option_context_set_summary(context, "VANXUM client to connect to Spice servers.");
    g_option_context_set_description(context, "Report bugs to VANXUM.");
    g_option_context_set_main_group(context, spice_cmdline_get_option_group());
    if (!g_option_context_parse (context, &argc, &argv, &error)) {
        g_print("option parsing failed: %s\n", error->message);
        exit(1);
    	}
    g_option_context_free(context);
	
    mainloop = g_main_loop_new(NULL, false);
    conn = connection_new();
    spice_cmdline_session_setup(conn->session);
    connection_connect(conn);
    if (connections > 0)
        g_main_loop_run(mainloop);
    g_main_loop_unref(mainloop); 
    return 0;
}


