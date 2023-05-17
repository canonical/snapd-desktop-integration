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

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "io.snapcraft.SnapDesktopIntegration.h"
#include "refresh_status.h"
#include "sdi-refresh-monitor.h"

struct _SdiRefreshMonitor {
  GObject parent_instance;

  SnapDesktopIntegration *skeleton;

  /* The list of current refresh popups */
  GList *refreshing_list;
};

G_DEFINE_TYPE(SdiRefreshMonitor, sdi_refresh_monitor, G_TYPE_OBJECT)

static gboolean dbus_handle_application_is_being_refreshed(
    SnapDesktopIntegration *skeleton, GDBusMethodInvocation *invocation,
    gchar *snap_name, gchar *lockFilePath, GVariant *extra_params,
    gpointer data) {
  handle_application_is_being_refreshed(snap_name, lockFilePath, extra_params,
                                        data);
  snap_desktop_integration_complete_application_is_being_refreshed(skeleton,
                                                                   invocation);
  return TRUE;
}

static gboolean dbus_handle_application_refresh_completed(
    SnapDesktopIntegration *skeleton, GDBusMethodInvocation *invocation,
    gchar *snap_name, GVariant *extra_params, gpointer data) {
  handle_application_refresh_completed(snap_name, extra_params, data);
  snap_desktop_integration_complete_application_refresh_completed(skeleton,
                                                                  invocation);
  return TRUE;
}

static gboolean dbus_handle_set_pulsed_progress(
    SnapDesktopIntegration *skeleton, GDBusMethodInvocation *invocation,
    gchar *snap_name, gchar *bar_text, GVariant *extra_params, gpointer data) {
  handle_set_pulsed_progress(snap_name, bar_text, extra_params, data);
  snap_desktop_integration_complete_application_refresh_pulsed(skeleton,
                                                               invocation);
  return TRUE;
}

static gboolean dbus_handle_set_percentage_progress(
    SnapDesktopIntegration *skeleton, GDBusMethodInvocation *invocation,
    gchar *snap_name, gchar *bar_text, gdouble percentage,
    GVariant *extra_params, gpointer data) {
  handle_set_percentage_progress(snap_name, bar_text, percentage, extra_params,
                                 data);
  snap_desktop_integration_complete_application_refresh_percentage(skeleton,
                                                                   invocation);
  return TRUE;
}

static void sdi_refresh_monitor_dispose(GObject *object) {
  SdiRefreshMonitor *self = SDI_REFRESH_MONITOR(object);

  g_clear_object(&self->skeleton);
  g_list_free_full(self->refreshing_list, (GDestroyNotify)refresh_state_free);

  G_OBJECT_CLASS(sdi_refresh_monitor_parent_class)->dispose(object);
}

void sdi_refresh_monitor_init(SdiRefreshMonitor *self) {
  self->skeleton = snap_desktop_integration_skeleton_new();
}

void sdi_refresh_monitor_class_init(SdiRefreshMonitorClass *klass) {
  G_OBJECT_CLASS(klass)->dispose = sdi_refresh_monitor_dispose;
}

SdiRefreshMonitor *sdi_refresh_monitor_new() {
  SdiRefreshMonitor *self = g_object_new(sdi_refresh_monitor_get_type(), NULL);
  return self;
}

gboolean sdi_refresh_monitor_start(SdiRefreshMonitor *self,
                                   GDBusConnection *connection,
                                   GError **error) {
  g_signal_connect(self->skeleton, "handle_application_is_being_refreshed",
                   G_CALLBACK(dbus_handle_application_is_being_refreshed),
                   self);
  g_signal_connect(self->skeleton, "handle_application_refresh_completed",
                   G_CALLBACK(dbus_handle_application_refresh_completed), self);
  g_signal_connect(self->skeleton, "handle_application_refresh_pulsed",
                   G_CALLBACK(dbus_handle_set_pulsed_progress), self);
  g_signal_connect(self->skeleton, "handle_application_refresh_percentage",
                   G_CALLBACK(dbus_handle_set_percentage_progress), self);
  return g_dbus_interface_skeleton_export(
      G_DBUS_INTERFACE_SKELETON(self->skeleton), connection,
      "/io/snapcraft/SnapDesktopIntegration", error);
}

RefreshState *sdi_refresh_monitor_lookup_application(SdiRefreshMonitor *self,
                                                     const char *app_name) {
  for (GList *link = self->refreshing_list; link != NULL; link = link->next) {
    RefreshState *state = (RefreshState *)link->data;
    if (0 == g_strcmp0(state->app_name, app_name)) {
      return state;
    }
  }
  return NULL;
}

void sdi_refresh_monitor_add_application(SdiRefreshMonitor *self,
                                         RefreshState *state) {
  self->refreshing_list = g_list_append(self->refreshing_list, state);
}

void sdi_refresh_monitor_remove_application(SdiRefreshMonitor *self,
                                            RefreshState *state) {
  self->refreshing_list = g_list_remove(self->refreshing_list, state);
}
