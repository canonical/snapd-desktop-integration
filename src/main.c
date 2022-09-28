#include "ds_state.h"
#include "themes.h"

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
handle_application_is_being_refreshed(GVariant *parameters,
                                      DsState  *state)
{

}

static void
handle_notifications_method_call(GDBusConnection       *connection,
                                 const gchar           *sender,
                                 const gchar           *object_path,
                                 const gchar           *interface_name,
                                 const gchar           *method_name,
                                 GVariant              *parameters,
                                 GDBusMethodInvocation *invocation,
                                 gpointer               user_data)
{
    g_print("Received call: %s; %s; %s\n", object_path, interface_name, method_name);
    if (g_strcmp0 (interface_name, "io.snapcraft.SnapDesktopIntegration") != 0) {
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_INTERFACE, "Unknown interface");
        return;
    }
    if (g_strcmp0 (method_name, "ApplicationIsBeingRefreshed") == 0) {
        handle_application_is_being_refreshed(parameters, (DsState*) user_data);
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown method");
}

static bool
register_dbus (GDBusConnection  *connection,
               DsState          *state,
               GError          **error)
{
    static const gchar introspection_xml[] =
    "<node>"
    "    <!-- io.snapcraft.SnapDesktopIntegration:"
    "        @short_description: Interface for desktop integration of Snapd"
    ""
    "        This D-Bus interface allows to achieve better integration of"
    "        snapd and snap utilities by allowing to show native windows"
    "        with messages instead of relying on system notifications,"
    "        which can be insufficient for our purposes."
    ""
    "        It is better to use an specific DBus interface instead of GtkActions"
    "        because this allows to implement a Qt version if desired without frictions."
    "        -->"
    "    <interface name=\"io.snapcraft.SnapDesktopIntegration\">"
    "        <!--"
    "            ApplicationIsBeingRefreshed:"
    "            @application_name: the name of the application name"
    "            @lock_file: the full path to the lock file, to allow"
    "                        the daemon to detect when the window must"
    "                        be closed."
    "           @extra_parameters: a dictionary with extra optional parameters."
    "                              Currently defined parameters are:"
    "                * icon: a string with the path to an icon."
    "           Method used to notify to the user that the snap that"
    "           they wanted to run is being refreshed, and they have"
    "           to wait until the process has finished."
    "       -->"
    "       <method name=\"ApplicationIsBeingRefreshed\">"
    "       <arg direction=\"in\" type=\"s\" name=\"application_name\"/>"
    "       <arg direction=\"in\" type=\"s\" name=\"lock_file\"/>"
    "       <arg direction=\"in\" type=\"a{sv}\" name=\"extra_parameters\"/>"

    "        </method>"
    "    </interface>"
    "</node>";

   g_autoptr(GDBusNodeInfo) node_info = g_dbus_node_info_new_for_xml (introspection_xml, error);
   if (node_info == NULL) {
      g_prefix_error(error, "Failed to parse D-Bus XML: ");
      return FALSE;
   }

   static const GDBusInterfaceVTable notifications_vtable = {
      handle_notifications_method_call,
      NULL,
      NULL,
   };
   if (g_dbus_connection_register_object(connection,
                                         "/io/snapcraft/SnapDesktopIntegration",
                                         node_info->interfaces[0],
                                         &notifications_vtable,
                                         (gpointer) state,
                                         NULL,
                                         error) == 0) {
      g_prefix_error(error, "Failed to register notifications object: ");
      return FALSE;
   }
   return TRUE;
}

static void
do_startup (GObject  *object,
            gpointer  data)
{
    DsState *state = (DsState *)data;
    notify_init("snapd-desktop-integration");

    state->settings = gtk_settings_get_default();
    state->client = snapd_client_new();
    state->app = GTK_APPLICATION (object);
    register_dbus (g_application_get_dbus_connection(G_APPLICATION (state->app)), state, NULL);
}

static void
do_activate (GObject  *object,
             gpointer  data)
{
    DsState *state = (DsState *)data;

    g_application_hold (G_APPLICATION (state->app)); // because, by default, there are no windows, so the application would quit

    if (snapd_socket_path != NULL) {
        snapd_client_set_socket_path(state->client, snapd_socket_path);
    } else if (g_getenv ("SNAP") != NULL) {
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
    g_autoptr (GtkApplication) app = NULL;
    g_autoptr(DsState) state = g_new0(DsState, 1);

    app = gtk_application_new ("io.snapcraft.SnapDesktopIntegration",
                               G_APPLICATION_FLAGS_NONE);
    g_signal_connect (G_OBJECT (app), "startup", G_CALLBACK (do_startup), state);
    g_signal_connect (G_OBJECT (app), "shutdown", G_CALLBACK (do_shutdown), state);
    g_signal_connect (G_OBJECT (app), "activate", G_CALLBACK (do_activate), state);

    g_application_add_main_option_entries (G_APPLICATION (app), entries);

    return g_application_run (G_APPLICATION (app), argc, argv);
}
