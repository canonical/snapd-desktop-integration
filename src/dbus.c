/*
 * Copyright (C) 2020-2022 Canonical Ltd
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "dbus.h"
#include "refresh_status.h"

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
    if (g_strcmp0 (interface_name, "io.snapcraft.SnapDesktopIntegration") != 0) {
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_INTERFACE, "Unknown interface");
        return;
    }
    if (g_strcmp0 (method_name, "ApplicationIsBeingRefreshed") == 0) {
        handle_application_is_being_refreshed(parameters, (DsState*) user_data);
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }
    if (g_strcmp0 (method_name, "ApplicationRefreshCompleted") == 0) {
        handle_close_application_window(parameters, (DsState*) user_data);
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown method");
}

gboolean
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
    "                        be closed, or an empty string to not monitor"
    "                        any lock file."
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
    "       <!--"
    "            ApplicationRefreshCompleted:"
    "            @application_name: the name of the application name"
    "           @extra_parameters: a dictionary with extra optional parameters."
    "           Method used to notify the daemon that the refresh is completed"
    "           and the dialog can be closed."
    "       -->"
    "       <method name=\"ApplicationRefreshCompleted\">"
    "       <arg direction=\"in\" type=\"s\" name=\"application_name\"/>"
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
