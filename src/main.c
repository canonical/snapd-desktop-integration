#include "ds_state.h"
#include "themes.h"
#include "dbus.h"

/* Number of second to wait after a theme change before checking for installed snaps. */
#define CHECK_THEME_TIMEOUT_SECONDS 1

static gchar *snapd_socket_path = NULL;

static GOptionEntry entries[] =
{
   { "snapd-socket-path", 0, 0, G_OPTION_ARG_FILENAME, &snapd_socket_path, "Snapd socket path", "PATH" },
   { NULL }
};

static void
queue_check_theme(DsState *state)
{
    /* Delay processing the theme, in case multiple themes are being changed at the same time. */
    g_clear_handle_id(&state->check_delay_timer_id, g_source_remove);
    state->check_delay_timer_id = g_timeout_add_seconds(
        CHECK_THEME_TIMEOUT_SECONDS, G_SOURCE_FUNC(get_themes_cb), state);
}

static void
do_startup (GObject  *object,
            gpointer  data)
{
    DsState *state = (DsState *)data;
    notify_init("snapd-desktop-integration");

    state->settings = gtk_settings_get_default();
    state->client = snapd_client_new();
    state->app = GTK_APPLICATION(object);
    register_dbus(g_application_get_dbus_connection(G_APPLICATION(state->app)), state, NULL);
}

static void
do_activate (GObject  *object,
             gpointer  data)
{
    DsState *state = (DsState *)data;

    g_application_hold(G_APPLICATION(state->app)); // because, by default, there are no windows, so the application would quit

    if (snapd_socket_path != NULL) {
        snapd_client_set_socket_path(state->client, snapd_socket_path);
    } else if (g_getenv("SNAP") != NULL) {
        snapd_client_set_socket_path(state->client, "/run/snapd-snap.socket");
    }

    /* Listen for theme changes. */
    g_signal_connect_swapped(state->settings, "notify::gtk-theme-name",
                     G_CALLBACK(queue_check_theme), state);
    g_signal_connect_swapped(state->settings, "notify::gtk-icon-theme-name",
                     G_CALLBACK(queue_check_theme), state);
    g_signal_connect_swapped(state->settings, "notify::gtk-cursor-theme-name",
                     G_CALLBACK(queue_check_theme), state);
    g_signal_connect_swapped(state->settings, "notify::gtk-sound-theme-name",
                     G_CALLBACK(queue_check_theme), state);
    get_themes_cb(state);
}

static void
do_shutdown (GObject  *object,
             gpointer  data)
{
    notify_uninit();
}

int
main(int argc, char **argv)
{
    g_autoptr(GtkApplication) app = NULL;
    g_autoptr(DsState) state = g_new0(DsState, 1);

    bindtextdomain ("snapd-desktop-integration", NULL);
    textdomain ("snapd-desktop-integration");
    app = gtk_application_new("io.snapcraft.SnapDesktopIntegration",
                               G_APPLICATION_FLAGS_NONE);
    g_signal_connect (G_OBJECT(app), "startup", G_CALLBACK(do_startup), state);
    g_signal_connect (G_OBJECT(app), "shutdown", G_CALLBACK(do_shutdown), state);
    g_signal_connect (G_OBJECT(app), "activate", G_CALLBACK(do_activate), state);

    g_application_add_main_option_entries(G_APPLICATION(app), entries);

    return g_application_run(G_APPLICATION(app), argc, argv);
}
