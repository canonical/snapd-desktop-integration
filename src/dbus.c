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
#include "io.snapcraft.SnapDesktopIntegration.h"

static gboolean
dbus_handle_application_is_being_refreshed (SnapDesktopIntegration *skeleton,
                                            GDBusMethodInvocation *invocation,
                                            gchar *snapName,
                                            gchar *lockFilePath,
                                            GVariantIter *extraParams,
                                            gpointer data) {

    handle_application_is_being_refreshed(snapName, lockFilePath, extraParams, data);
    snap_desktop_integration_complete_application_is_being_refreshed(skeleton, invocation);
    return TRUE;
}

static gboolean
dbus_handle_close_application_window (SnapDesktopIntegration *skeleton,
                                      GDBusMethodInvocation *invocation,
                                      gchar *snapName,
                                      GVariantIter *extraParams,
                                      gpointer data) {

    handle_close_application_window(snapName, extraParams, data);
    snap_desktop_integration_complete_application_refresh_completed(skeleton, invocation);
    return TRUE;
}

void
register_dbus (GDBusConnection  *connection,
               DsState          *state,
               GError          **error)
{

    SnapDesktopIntegration *skeleton = snap_desktop_integration_skeleton_new();
    g_signal_connect(skeleton, "handle_application_is_being_refreshed",
                     G_CALLBACK (dbus_handle_application_is_being_refreshed),
                     state);
    g_signal_connect(skeleton, "handle_application_refresh_completed",
                     G_CALLBACK (dbus_handle_close_application_window),
                     state);
    g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON(skeleton),
                                      connection,
                                      "/io/snapcraft/SnapDesktopIntegration",
                                      NULL);
}
