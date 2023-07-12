/*
 * Copyright (C) 2023 Canonical Ltd
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
#include <libnotify/notify.h>

#include "sdi-reboot-monitor.h"

// Poll time for getting changes from snapd to detect if reboot required.
#define GET_CHANGES_TIMEOUT_SECONDS 5

struct _SdiRebootMonitor {
  GObject parent_instance;

  // Connection to snapd.
  SnapdClient *client;

  // Timer for next changes poll.
  guint timer_id;

  // Notification shown to the user.
  NotifyNotification *notification;

  GCancellable *cancellable;
};

G_DEFINE_TYPE(SdiRebootMonitor, sdi_reboot_monitor, G_TYPE_OBJECT)

static gboolean get_changes(SdiRebootMonitor *self);

static void show_notification(SdiRebootMonitor *self) {
  if (self->notification != NULL) {
    return;
  }

  self->notification = notify_notification_new(
      _("Restart Required"),
      _("The system needs to be restarted to install updates"),
      "dialog-information");
  g_autoptr(GError) error = NULL;
  if (!notify_notification_show(self->notification, &error)) {
    g_warning("Failed to show notification: %s", error->message);
  }
}

static void close_notification(SdiRebootMonitor *self) {
  if (self->notification == NULL) {
    return;
  }

  g_autoptr(GError) error = NULL;
  if (!notify_notification_close(self->notification, &error)) {
    g_warning("Failed to close notification: %s", error->message);
  }
  g_clear_object(&self->notification);
}

static gboolean task_requires_reboot(SnapdTask *task) {
  return g_str_equal(snapd_task_get_status(task), "Wait");
}

static gboolean change_requires_reboot(SnapdChange *change) {
  GPtrArray *tasks = snapd_change_get_tasks(change);
  for (guint i = 0; i < tasks->len; i++) {
    SnapdTask *task = g_ptr_array_index(tasks, i);
    if (task_requires_reboot(task)) {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean changes_require_reboot(GPtrArray *changes) {
  for (guint i = 0; i < changes->len; i++) {
    SnapdChange *change = g_ptr_array_index(changes, i);
    if (change_requires_reboot(change)) {
      return TRUE;
    }
  }

  return FALSE;
}

static void get_changes_cb(GObject *object, GAsyncResult *result,
                           gpointer user_data) {
  SdiRebootMonitor *self = user_data;

  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) changes =
      snapd_client_get_changes_finish(SNAPD_CLIENT(object), result, &error);
  if (changes == NULL) {
    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      return;
    }

    g_warning("Failed to get changes: %s", error->message);
    return;
  }

  if (changes_require_reboot(changes)) {
    show_notification(self);
  } else {
    close_notification(self);
  }

  // Reschedule next check.
  g_clear_handle_id(&self->timer_id, g_source_remove);
  self->timer_id = g_timeout_add_seconds(GET_CHANGES_TIMEOUT_SECONDS,
                                         G_SOURCE_FUNC(get_changes), self);
}

static gboolean get_changes(SdiRebootMonitor *self) {
  snapd_client_get_changes_async(self->client, SNAPD_CHANGE_FILTER_IN_PROGRESS,
                                 NULL, self->cancellable, get_changes_cb, self);
  self->timer_id = 0;
  return G_SOURCE_REMOVE;
}

static void get_system_information_cb(GObject *object, GAsyncResult *result,
                                      gpointer user_data) {
  SdiRebootMonitor *self = user_data;

  g_autoptr(GError) error = NULL;
  g_autoptr(SnapdSystemInformation) info =
      snapd_client_get_system_information_finish(SNAPD_CLIENT(object), result,
                                                 &error);
  if (info == NULL) {
    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      return;
    }

    g_warning("Failed to snapd system information changes: %s", error->message);
    return;
  }

  // Reboot notifications are only done on Ubuntu Core desktop.
  if (g_str_equal(snapd_system_information_get_os_id(info), "ubuntu-core")) {
    get_changes(self);
  }
}

static void sdi_reboot_monitor_dispose(GObject *object) {
  SdiRebootMonitor *self = SDI_REBOOT_MONITOR(object);

  g_cancellable_cancel(self->cancellable);

  g_clear_object(&self->client);
  g_clear_handle_id(&self->timer_id, g_source_remove);
  g_clear_object(&self->notification);
  g_clear_object(&self->cancellable);

  G_OBJECT_CLASS(sdi_reboot_monitor_parent_class)->dispose(object);
}

void sdi_reboot_monitor_init(SdiRebootMonitor *self) {
  self->cancellable = g_cancellable_new();
}

void sdi_reboot_monitor_class_init(SdiRebootMonitorClass *klass) {
  G_OBJECT_CLASS(klass)->dispose = sdi_reboot_monitor_dispose;
}

SdiRebootMonitor *sdi_reboot_monitor_new(SnapdClient *client) {
  SdiRebootMonitor *self = g_object_new(sdi_reboot_monitor_get_type(), NULL);

  self->client = g_object_ref(client);

  return self;
}

void sdi_reboot_monitor_start(SdiRebootMonitor *self) {
  // Check if this a core system.
  snapd_client_get_system_information_async(self->client, self->cancellable,
                                            get_system_information_cb, self);
}
