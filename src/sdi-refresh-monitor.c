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

#include <gtk/gtk.h>

#include "io.snapcraft.SnapDesktopIntegration.h"
#include "sdi-refresh-dialog.h"
#include "sdi-refresh-monitor.h"

struct _SdiRefreshMonitor {
  GObject parent_instance;

  SnapDesktopIntegration *skeleton;

  GList *dialogs;
};

G_DEFINE_TYPE(SdiRefreshMonitor, sdi_refresh_monitor, G_TYPE_OBJECT)

static SdiRefreshDialog *lookup_dialog(SdiRefreshMonitor *self,
                                       const char *app_name) {
  for (GList *link = self->dialogs; link != NULL; link = link->next) {
    SdiRefreshDialog *dialog = (SdiRefreshDialog *)link->data;
    if (0 == g_strcmp0(sdi_refresh_dialog_get_app_name(dialog), app_name)) {
      return dialog;
    }
  }
  return NULL;
}

static void handle_extra_params(SdiRefreshMonitor *self,
                                SdiRefreshDialog *dialog,
                                GVariant *extra_params) {
  GVariantIter iter;
  GVariant *value;
  const gchar *key;

  // Do a copy to allow manage the iter in other places if needed
  g_variant_iter_init(&iter, extra_params);
  while (g_variant_iter_next(&iter, "{&sv}", &key, &value)) {
    if (!g_strcmp0(key, "message")) {
      sdi_refresh_dialog_set_message(dialog, g_variant_get_string(value, NULL));
    } else if (!g_strcmp0(key, "title")) {
      sdi_refresh_dialog_set_title(dialog, g_variant_get_string(value, NULL));
    } else if (!g_strcmp0(key, "icon")) {
      sdi_refresh_dialog_set_icon(dialog, g_variant_get_string(value, NULL));
    } else if (!g_strcmp0(key, "icon_image")) {
      sdi_refresh_dialog_set_icon_image(dialog,
                                        g_variant_get_string(value, NULL));
    } else if (!g_strcmp0(key, "wait_change_in_lock_file")) {
      sdi_refresh_dialog_set_wait_change_in_lock_file(dialog);
    } else if (!g_strcmp0(key, "desktop_file")) {
      sdi_refresh_dialog_set_desktop_file(dialog,
                                          g_variant_get_string(value, NULL));
    }
    g_variant_unref(value);
  }
}

static gboolean dialog_destroy_cb(SdiRefreshMonitor *self,
                                  SdiRefreshDialog *dialog) {
  self->dialogs = g_list_remove(self->dialogs, dialog);
  return TRUE;
}

static gboolean handle_application_is_being_refreshed_cb(
    SdiRefreshMonitor *self, GDBusMethodInvocation *invocation,
    const gchar *snap_name, const gchar *lock_file_path,
    GVariant *extra_params) {
  SdiRefreshDialog *dialog = lookup_dialog(self, snap_name);

  if (dialog != NULL) {
    gtk_window_present(GTK_WINDOW(dialog));
    handle_extra_params(self, dialog, extra_params);
  } else {
    dialog = sdi_refresh_dialog_new(snap_name, lock_file_path);
    self->dialogs = g_list_append(self->dialogs, dialog);
    g_signal_connect_swapped(G_OBJECT(dialog), "destroy",
                             G_CALLBACK(dialog_destroy_cb), self);
    gtk_window_present(GTK_WINDOW(dialog));
    handle_extra_params(self, dialog, extra_params);
  }

  snap_desktop_integration_complete_application_is_being_refreshed(
      self->skeleton, invocation);

  return TRUE;
}

static gboolean handle_application_refresh_completed_cb(
    SdiRefreshMonitor *self, GDBusMethodInvocation *invocation,
    const gchar *snap_name, GVariant *extra_params) {
  SdiRefreshDialog *dialog = lookup_dialog(self, snap_name);
  if (dialog != NULL) {
    gtk_window_destroy(GTK_WINDOW(dialog));
  }

  snap_desktop_integration_complete_application_refresh_completed(
      self->skeleton, invocation);

  return TRUE;
}

static gboolean handle_set_pulsed_progress_cb(SdiRefreshMonitor *self,
                                              GDBusMethodInvocation *invocation,
                                              const gchar *snap_name,
                                              const gchar *bar_text,
                                              GVariant *extra_params) {
  SdiRefreshDialog *dialog = lookup_dialog(self, snap_name);
  if (dialog != NULL) {
    sdi_refresh_dialog_set_pulsed_progress(dialog, bar_text);
    handle_extra_params(self, dialog, extra_params);
  }

  snap_desktop_integration_complete_application_refresh_pulsed(self->skeleton,
                                                               invocation);

  return TRUE;
}

static gboolean
handle_set_percentage_progress_cb(SdiRefreshMonitor *self,
                                  GDBusMethodInvocation *invocation,
                                  const gchar *snap_name, const gchar *bar_text,
                                  gdouble percentage, GVariant *extra_params) {
  SdiRefreshDialog *dialog = lookup_dialog(self, snap_name);
  if (dialog != NULL) {
    sdi_refresh_dialog_set_percentage_progress(dialog, bar_text, percentage);
    handle_extra_params(self, dialog, extra_params);
  }

  snap_desktop_integration_complete_application_refresh_percentage(
      self->skeleton, invocation);

  return TRUE;
}

static void sdi_refresh_monitor_dispose(GObject *object) {
  SdiRefreshMonitor *self = SDI_REFRESH_MONITOR(object);

  g_clear_object(&self->skeleton);
  for (GList *link = self->dialogs; link != NULL; link = link->next) {
    SdiRefreshDialog *dialog = (SdiRefreshDialog *)link->data;
    gtk_window_destroy(GTK_WINDOW(dialog));
  }
  g_list_free(self->dialogs);

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
  g_signal_connect_swapped(
      self->skeleton, "handle_application_is_being_refreshed",
      G_CALLBACK(handle_application_is_being_refreshed_cb), self);
  g_signal_connect_swapped(
      self->skeleton, "handle_application_refresh_completed",
      G_CALLBACK(handle_application_refresh_completed_cb), self);
  g_signal_connect_swapped(self->skeleton, "handle_application_refresh_pulsed",
                           G_CALLBACK(handle_set_pulsed_progress_cb), self);
  g_signal_connect_swapped(self->skeleton,
                           "handle_application_refresh_percentage",
                           G_CALLBACK(handle_set_percentage_progress_cb), self);
  return g_dbus_interface_skeleton_export(
      G_DBUS_INTERFACE_SKELETON(self->skeleton), connection,
      "/io/snapcraft/SnapDesktopIntegration", error);
}
