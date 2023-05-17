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
#include <libnotify/notify.h>

#include "sdi-theme-monitor.h"

struct _SdiThemeMonitor {
  GObject parent_instance;

  // Theme settings.
  GtkSettings *settings;

  /* Timer to delay checking after theme changes */
  guint check_delay_timer_id;

  // Name of current themes and their status in snapd.
  gchar *gtk_theme_name;
  SnapdThemeStatus gtk_theme_status;
  gchar *icon_theme_name;
  SnapdThemeStatus icon_theme_status;
  gchar *cursor_theme_name;
  SnapdThemeStatus cursor_theme_status;
  gchar *sound_theme_name;
  SnapdThemeStatus sound_theme_status;

  /* The desktop notifications */
  NotifyNotification *install_notification;
  NotifyNotification *progress_notification;

  // Connection to snapd.
  SnapdClient *client;
};

G_DEFINE_TYPE(SdiThemeMonitor, sdi_theme_monitor, G_TYPE_OBJECT)

/* Number of second to wait after a theme change before checking for installed
 * snaps. */
#define CHECK_THEME_TIMEOUT_SECONDS 1

static void install_themes_cb(GObject *object, GAsyncResult *result,
                              gpointer user_data) {
  SdiThemeMonitor *self = user_data;
  g_autoptr(GError) error = NULL;

  if (snapd_client_install_themes_finish(SNAPD_CLIENT(object), result,
                                         &error)) {
    g_message("Installation complete.\n");
    notify_notification_update(
        self->progress_notification, _("Installing missing theme snaps:"),
        /// TRANSLATORS: installing a missing theme snap succeed
        _("Complete."), "dialog-information");
  } else {
    g_message("Installation failed: %s\n", error->message);
    const gchar *error_message;
    switch (error->code) {
    case SNAPD_ERROR_AUTH_CANCELLED:
      /// TRANSLATORS: installing a missing theme snap was cancelled by the user
      error_message = _("Canceled by the user.");
      break;
    default:
      /// TRANSLATORS: installing a missing theme snap failed
      error_message = _("Failed.");
      break;
    }
    notify_notification_update(self->progress_notification,
                               _("Installing missing theme snaps:"),
                               error_message, "dialog-information");
  }

  notify_notification_show(self->progress_notification, NULL);
  g_clear_object(&self->progress_notification);
}

static void notification_closed_cb(NotifyNotification *notification,
                                   SdiThemeMonitor *self) {
  /* Notification has been closed: */
  g_clear_object(&self->install_notification);
}

static void notify_cb(NotifyNotification *notification, gchar *action,
                      gpointer user_data) {
  SdiThemeMonitor *self = user_data;

  if ((strcmp(action, "yes") == 0) || (strcmp(action, "default") == 0)) {
    g_message("Installing missing theme snaps...\n");
    self->progress_notification = notify_notification_new(
        _("Installing missing theme snaps:"), "...", "dialog-information");
    notify_notification_show(self->progress_notification, NULL);

    g_autoptr(GPtrArray) gtk_theme_names = g_ptr_array_new();
    if (self->gtk_theme_status == SNAPD_THEME_STATUS_AVAILABLE) {
      g_ptr_array_add(gtk_theme_names, self->gtk_theme_name);
    }
    g_ptr_array_add(gtk_theme_names, NULL);
    g_autoptr(GPtrArray) icon_theme_names = g_ptr_array_new();
    if (self->icon_theme_status == SNAPD_THEME_STATUS_AVAILABLE) {
      g_ptr_array_add(icon_theme_names, self->icon_theme_name);
    }
    if (self->cursor_theme_status == SNAPD_THEME_STATUS_AVAILABLE) {
      g_ptr_array_add(icon_theme_names, self->cursor_theme_name);
    }
    g_ptr_array_add(icon_theme_names, NULL);
    g_autoptr(GPtrArray) sound_theme_names = g_ptr_array_new();
    if (self->sound_theme_status == SNAPD_THEME_STATUS_AVAILABLE) {
      g_ptr_array_add(sound_theme_names, self->sound_theme_name);
    }
    g_ptr_array_add(sound_theme_names, NULL);
    snapd_client_install_themes_async(
        self->client, (gchar **)gtk_theme_names->pdata,
        (gchar **)icon_theme_names->pdata, (gchar **)sound_theme_names->pdata,
        NULL, NULL, NULL, install_themes_cb, self);
  }
}

static void show_install_notification(SdiThemeMonitor *self) {
  /* If we've already displayed a notification, do nothing */
  if (self->install_notification != NULL)
    return;

  self->install_notification = notify_notification_new(
      _("Some required theme snaps are missing."),
      _("Would you like to install them now?"), "dialog-question");
  g_signal_connect(self->install_notification, "closed",
                   G_CALLBACK(notification_closed_cb), self);
  notify_notification_set_timeout(self->install_notification,
                                  NOTIFY_EXPIRES_NEVER);
  notify_notification_add_action(
      self->install_notification, "yes",
      /// TRANSLATORS: answer to the question "Would you like to install them
      /// now?" referred to snap themes
      _("Yes"), notify_cb, self, NULL);
  notify_notification_add_action(
      self->install_notification, "no",
      /// TRANSLATORS: answer to the question "Would you like to install them
      /// now?" referred to snap themes
      _("No"), notify_cb, self, NULL);
  notify_notification_add_action(self->install_notification, "default",
                                 "default", notify_cb, self, NULL);

  notify_notification_show(self->install_notification, NULL);
}

static void check_themes_cb(GObject *object, GAsyncResult *result,
                            gpointer user_data) {
  SdiThemeMonitor *self = user_data;

  g_autoptr(GHashTable) gtk_theme_status = NULL;
  g_autoptr(GHashTable) icon_theme_status = NULL;
  g_autoptr(GHashTable) sound_theme_status = NULL;
  g_autoptr(GError) error = NULL;
  if (!snapd_client_check_themes_finish(SNAPD_CLIENT(object), result,
                                        &gtk_theme_status, &icon_theme_status,
                                        &sound_theme_status, &error)) {
    g_warning("Could not check themes: %s", error->message);
    return;
  }

  if (self->gtk_theme_name)
    self->gtk_theme_status = GPOINTER_TO_INT(
        g_hash_table_lookup(gtk_theme_status, self->gtk_theme_name));
  else
    self->gtk_theme_status = SNAPD_THEME_STATUS_UNAVAILABLE;
  if (self->icon_theme_name)
    self->icon_theme_status = GPOINTER_TO_INT(
        g_hash_table_lookup(icon_theme_status, self->icon_theme_name));
  else
    self->icon_theme_status = SNAPD_THEME_STATUS_UNAVAILABLE;
  if (self->cursor_theme_name)
    self->cursor_theme_status = GPOINTER_TO_INT(
        g_hash_table_lookup(icon_theme_status, self->cursor_theme_name));
  else
    self->cursor_theme_status = SNAPD_THEME_STATUS_UNAVAILABLE;
  if (self->sound_theme_name)
    self->sound_theme_status = GPOINTER_TO_INT(
        g_hash_table_lookup(sound_theme_status, self->sound_theme_name));
  else
    self->sound_theme_status = SNAPD_THEME_STATUS_UNAVAILABLE;

  gboolean themes_available =
      self->gtk_theme_status == SNAPD_THEME_STATUS_AVAILABLE ||
      self->icon_theme_status == SNAPD_THEME_STATUS_AVAILABLE ||
      self->cursor_theme_status == SNAPD_THEME_STATUS_AVAILABLE ||
      self->sound_theme_status == SNAPD_THEME_STATUS_AVAILABLE;

  if (!themes_available) {
    g_message("All available theme snaps installed\n");
    return;
  }

  g_message("Missing theme snaps\n");

  show_install_notification(self);
}

static gboolean get_themes_cb(SdiThemeMonitor *self) {
  self->check_delay_timer_id = 0;

  g_autofree gchar *gtk_theme_name = NULL;
  g_autofree gchar *icon_theme_name = NULL;
  g_autofree gchar *cursor_theme_name = NULL;
  g_autofree gchar *sound_theme_name = NULL;
  g_object_get(self->settings, "gtk-theme-name", &gtk_theme_name,
               "gtk-icon-theme-name", &icon_theme_name, "gtk-cursor-theme-name",
               &cursor_theme_name, "gtk-sound-theme-name", &sound_theme_name,
               NULL);

  /* If nothing has changed, we're done */
  if (g_strcmp0(self->gtk_theme_name, gtk_theme_name) == 0 &&
      g_strcmp0(self->icon_theme_name, icon_theme_name) == 0 &&
      g_strcmp0(self->cursor_theme_name, cursor_theme_name) == 0 &&
      g_strcmp0(self->sound_theme_name, sound_theme_name) == 0) {
    return G_SOURCE_REMOVE;
  }

  g_message("New theme: gtk=%s icon=%s cursor=%s, sound=%s", gtk_theme_name,
            icon_theme_name, cursor_theme_name, sound_theme_name);

  g_free(self->gtk_theme_name);
  self->gtk_theme_name = g_steal_pointer(&gtk_theme_name);
  self->gtk_theme_status = 0;

  g_free(self->icon_theme_name);
  self->icon_theme_name = g_steal_pointer(&icon_theme_name);
  self->icon_theme_status = 0;

  g_free(self->cursor_theme_name);
  self->cursor_theme_name = g_steal_pointer(&cursor_theme_name);
  self->cursor_theme_status = 0;

  g_free(self->sound_theme_name);
  self->sound_theme_name = g_steal_pointer(&sound_theme_name);
  self->sound_theme_status = 0;

  g_autoptr(GPtrArray) gtk_theme_names = g_ptr_array_new();
  if (self->gtk_theme_name)
    g_ptr_array_add(gtk_theme_names, self->gtk_theme_name);
  g_ptr_array_add(gtk_theme_names, NULL);

  g_autoptr(GPtrArray) icon_theme_names = g_ptr_array_new();
  if (self->icon_theme_name)
    g_ptr_array_add(icon_theme_names, self->icon_theme_name);
  if (self->cursor_theme_name)
    g_ptr_array_add(icon_theme_names, self->cursor_theme_name);
  g_ptr_array_add(icon_theme_names, NULL);

  g_autoptr(GPtrArray) sound_theme_names = g_ptr_array_new();
  if (self->sound_theme_name)
    g_ptr_array_add(sound_theme_names, self->sound_theme_name);
  g_ptr_array_add(sound_theme_names, NULL);

  snapd_client_check_themes_async(
      self->client, (gchar **)gtk_theme_names->pdata,
      (gchar **)icon_theme_names->pdata, (gchar **)sound_theme_names->pdata,
      NULL, check_themes_cb, self);

  return G_SOURCE_REMOVE;
}

static void queue_check_theme(SdiThemeMonitor *self) {
  /* Delay processing the theme, in case multiple themes are being changed at
   * the same time. */
  g_clear_handle_id(&self->check_delay_timer_id, g_source_remove);
  self->check_delay_timer_id = g_timeout_add_seconds(
      CHECK_THEME_TIMEOUT_SECONDS, G_SOURCE_FUNC(get_themes_cb), self);
}

static void sdi_theme_monitor_dispose(GObject *object) {
  SdiThemeMonitor *self = SDI_THEME_MONITOR(object);

  g_clear_object(&self->settings);
  g_clear_handle_id(&self->check_delay_timer_id, g_source_remove);
  g_clear_pointer(&self->gtk_theme_name, g_free);
  g_clear_pointer(&self->icon_theme_name, g_free);
  g_clear_pointer(&self->cursor_theme_name, g_free);
  g_clear_pointer(&self->sound_theme_name, g_free);
  g_clear_object(&self->install_notification);
  g_clear_object(&self->progress_notification);
  g_clear_object(&self->client);

  G_OBJECT_CLASS(sdi_theme_monitor_parent_class)->dispose(object);
}

void sdi_theme_monitor_init(SdiThemeMonitor *self) {
  self->settings = gtk_settings_get_default();
}

void sdi_theme_monitor_class_init(SdiThemeMonitorClass *klass) {
  G_OBJECT_CLASS(klass)->dispose = sdi_theme_monitor_dispose;
}

SdiThemeMonitor *sdi_theme_monitor_new(SnapdClient *client) {
  SdiThemeMonitor *self = g_object_new(sdi_theme_monitor_get_type(), NULL);
  self->client = g_object_ref(client);
  return self;
}

void sdi_theme_monitor_start(SdiThemeMonitor *self) {
  /* Listen for theme changes. */
  g_signal_connect_swapped(self->settings, "notify::gtk-theme-name",
                           G_CALLBACK(queue_check_theme), self);
  g_signal_connect_swapped(self->settings, "notify::gtk-icon-theme-name",
                           G_CALLBACK(queue_check_theme), self);
  g_signal_connect_swapped(self->settings, "notify::gtk-cursor-theme-name",
                           G_CALLBACK(queue_check_theme), self);
  g_signal_connect_swapped(self->settings, "notify::gtk-sound-theme-name",
                           G_CALLBACK(queue_check_theme), self);
  get_themes_cb(self);
}
