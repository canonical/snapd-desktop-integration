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
#include "io.snapcraft.SnapDesktopIntegration.h"
#include "refresh_status.h"

static gboolean dbus_handle_application_is_being_refreshed(
    SnapDesktopIntegration *skeleton, GDBusMethodInvocation *invocation,
    gchar *snapName, gchar *lockFilePath, GVariantIter *extraParams,
    gpointer data) {

  handle_application_is_being_refreshed(snapName, lockFilePath, extraParams,
                                        data);
  snap_desktop_integration_complete_application_is_being_refreshed(skeleton,
                                                                   invocation);
  return TRUE;
}

static gboolean dbus_handle_close_application_window(
    SnapDesktopIntegration *skeleton, GDBusMethodInvocation *invocation,
    gchar *snapName, GVariantIter *extraParams, gpointer data) {

  handle_close_application_window(snapName, extraParams, data);
  snap_desktop_integration_complete_application_refresh_completed(skeleton,
                                                                  invocation);
  return TRUE;
}

static gboolean dbus_handle_set_pulsed_progress(
    SnapDesktopIntegration *skeleton, GDBusMethodInvocation *invocation,
    gchar *snapName, gchar *barText, GVariantIter *extraParams, gpointer data) {

  handle_set_pulsed_progress(snapName, barText, extraParams, data);
  snap_desktop_integration_complete_application_refresh_pulsed(skeleton,
                                                               invocation);
  return TRUE;
}

static gboolean dbus_handle_set_percentage_progress(
    SnapDesktopIntegration *skeleton, GDBusMethodInvocation *invocation,
    gchar *snapName, gchar *barText, gdouble percentage,
    GVariantIter *extraParams, gpointer data) {

  handle_set_percentage_progress(snapName, barText, percentage, extraParams,
                                 data);
  snap_desktop_integration_complete_application_refresh_percentage(skeleton,
                                                                   invocation);
  return TRUE;
}

gboolean register_dbus(GDBusConnection *connection, DsState *state,
                       GError **error) {

  state->skeleton = snap_desktop_integration_skeleton_new();
  g_signal_connect(state->skeleton, "handle_application_is_being_refreshed",
                   G_CALLBACK(dbus_handle_application_is_being_refreshed),
                   state);
  g_signal_connect(state->skeleton, "handle_application_refresh_completed",
                   G_CALLBACK(dbus_handle_close_application_window), state);
  g_signal_connect(state->skeleton, "handle_application_refresh_pulsed",
                   G_CALLBACK(dbus_handle_set_pulsed_progress), state);
  g_signal_connect(state->skeleton, "handle_application_refresh_percentage",
                   G_CALLBACK(dbus_handle_set_percentage_progress), state);
  return g_dbus_interface_skeleton_export(
      G_DBUS_INTERFACE_SKELETON(state->skeleton), connection,
      "/io/snapcraft/SnapDesktopIntegration", error);
}
