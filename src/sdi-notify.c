/*
 * Copyright (C) 2024 Canonical Ltd
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

// .desktop files for snap store.
#define SNAP_STORE "snap-store_snap-store.desktop"
#define SNAP_STORE_UPDATES "snap-store_show-updates.desktop"

#include "sdi-notify.h"
#include <gio/gdesktopappinfo.h>
#include <glib/gi18n.h>
#include <libnotify/notify.h>

#include "io.snapcraft.PrivilegedDesktopLauncher.h"
#include "sdi-helpers.h"

enum { PROP_APPLICATION = 1, PROP_LAST };

struct _SdiNotify {
  GObject parent_instance;

  GApplication *application;
};

G_DEFINE_TYPE(SdiNotify, sdi_notify, G_TYPE_OBJECT)

static gboolean launch_desktop(GApplication *app, const gchar *desktop_file) {
  g_autofree gchar *full_desktop_path = NULL;
  g_autofree gchar *desktop_file2 = NULL;
  if (*desktop_file == '/') {
    full_desktop_path = g_strdup(desktop_file);
    desktop_file2 = g_path_get_basename(desktop_file);
  } else {
    full_desktop_path = g_build_path("/", "/var/lib/snapd/desktop/applications",
                                     desktop_file, NULL);
    desktop_file2 = g_strdup(desktop_file);
  }
  if (!g_file_test(full_desktop_path, G_FILE_TEST_EXISTS)) {
    return FALSE;
  }
  g_autoptr(PrivilegedDesktopLauncher) launcher = NULL;

  launcher = privileged_desktop_launcher__proxy_new_sync(
      g_application_get_dbus_connection(app), G_DBUS_PROXY_FLAGS_NONE,
      "io.snapcraft.Launcher", "/io/snapcraft/PrivilegedDesktopLauncher", NULL,
      NULL);
  privileged_desktop_launcher__call_open_desktop_entry_sync(
      launcher, desktop_file2, NULL, NULL);
  return TRUE;
}

static void show_updates(SdiNotify *self) {
#ifdef DEBUG_TESTS
  g_signal_emit_by_name(self, "notification-closed", "show-updates");
#endif
  if (!launch_desktop(self->application, SNAP_STORE_UPDATES)) {
    launch_desktop(self->application, SNAP_STORE);
  }
}

static void sdi_notify_action_show_updates(GActionGroup *action_group,
                                           GVariant *str_data,
                                           SdiNotify *self) {
  show_updates(self);
}

static void sdi_notify_action_ignore(GActionGroup *action_group,
                                     GVariant *app_list, SdiNotify *self) {
  gsize len;
  g_autofree gchar **apps = (gchar **)g_variant_get_strv(app_list, &len);

  g_return_if_fail(apps != NULL);
  for (gsize i = 0; i < len; i++) {
    g_signal_emit_by_name(self, "ignore-snap-event", apps[i]);
  }
#ifdef DEBUG_TESTS
  g_signal_emit_by_name(self, "notification-closed", "ignore-snaps");
#endif
}

/* Currently, due to the way Snapd creates the .desktop files, the notifications
 * created with GNotify don't show the application icon in the upper-left
 * corner, putting instead the "generic gears" icon. This is the reason why, by
 * default, we use libnotify. This problem is being tackled by the snapd people,
 * so, in the future, GNotify would be the prefered choice.
 */
#ifndef USE_GNOTIFY

static GVariant *get_snap_list(GListModel *snaps) {
  if (snaps == NULL) {
    return NULL;
  }
  g_autoptr(GVariantBuilder) builder =
      g_variant_builder_new(G_VARIANT_TYPE("as"));
  for (int i = 0; i < g_list_model_get_n_items(snaps); i++) {
    g_autoptr(SnapdSnap) snap = g_list_model_get_item(snaps, i);
    g_variant_builder_add(builder, "s", snapd_snap_get_name(snap));
  }
  return g_variant_ref_sink(g_variant_builder_end(builder));
}

static gchar *get_icon_name_from_gicon(GIcon *icon) {
  if (icon == NULL) {
    return NULL;
  }

  if (G_IS_THEMED_ICON(icon)) {
    GThemedIcon *themed_icon = G_THEMED_ICON(icon);
    const gchar *const *names = g_themed_icon_get_names(themed_icon);
    if (names == NULL) {
      return NULL;
    }
    return g_strdup(names[0]);
  }

  if (G_IS_FILE_ICON(icon)) {
    GFileIcon *file_icon = G_FILE_ICON(icon);
    // the value returned by g_file_icon_get_file() is owned by the instance
    return g_file_get_path(g_file_icon_get_file(file_icon));
  }
  return NULL;
}

typedef struct {
  SdiNotify *self;
  GVariant *snaps;
} IgnoreNotifyData;

static IgnoreNotifyData *ignore_notify_data_new(SdiNotify *self,
                                                GVariant *snaps) {
  IgnoreNotifyData *data = g_malloc0(sizeof(IgnoreNotifyData));
  data->self = g_object_ref(self);
  data->snaps = g_variant_ref(snaps);
  return data;
}

static void ignore_notify_data_free(IgnoreNotifyData *data) {
  g_object_unref(data->self);
  g_variant_unref(data->snaps);
  g_free(data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(IgnoreNotifyData, ignore_notify_data_free)

G_DEFINE_AUTOPTR_CLEANUP_FUNC(NotifyNotification, g_object_unref)

typedef struct {
  SdiNotify *self;
  gchar *desktop;
} LaunchUpdatedApp;

static LaunchUpdatedApp *launch_updated_app_new(SdiNotify *self,
                                                const gchar *desktop) {
  LaunchUpdatedApp *data = g_malloc0(sizeof(LaunchUpdatedApp));
  data->self = g_object_ref(self);
  data->desktop = g_strdup(desktop);
  return data;
}

static void launch_updated_app_free(void *user_data) {
  LaunchUpdatedApp *data = (LaunchUpdatedApp *)user_data;
  g_object_unref(data->self);
  g_free(data->desktop);
  g_free(data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(LaunchUpdatedApp, launch_updated_app_free)

static void app_close_notification(NotifyNotification *notification,
                                   char *action, SdiNotify *self) {
#ifdef DEBUG_TESTS
  g_signal_emit_by_name(self, "notification-closed", "close-notification");
#endif
  g_object_unref(notification);
}

static void app_launch_updated(NotifyNotification *notification, char *action,
                               LaunchUpdatedApp *data) {
#ifdef DEBUG_TESTS
  g_autofree gchar *param =
      g_strdup_printf("app-launch-updated %s", data->desktop);
  g_signal_emit_by_name(data->self, "notification-closed", param);
#endif
  launch_desktop(data->self->application, (const gchar *)data->desktop);
  g_object_unref(notification);
}

static void app_show_updates(NotifyNotification *notification, char *action,
                             SdiNotify *self) {
  show_updates(self);
  g_object_unref(notification);
}

static void app_ignore_snaps_notification(NotifyNotification *notification,
                                          char *action,
                                          IgnoreNotifyData *data) {
  sdi_notify_action_ignore(NULL, data->snaps, data->self);
  g_object_unref(notification);
}

static void show_pending_update_notification(SdiNotify *self,
                                             const gchar *title,
                                             const gchar *body, GIcon *icon,
                                             GListModel *snaps,
                                             gboolean allow_to_ignore) {
  g_autofree gchar *icon_name = get_icon_name_from_gicon(icon);
  // Don't use g_autoptr because it must survive for the actions
  NotifyNotification *notification =
      notify_notification_new(title, body, icon_name);
  if (icon_name != NULL) {
    // don't use g_autoptr with the GVariant because it is consumed in set_hint
    notify_notification_set_hint(notification, "image-path",
                                 g_variant_new_string(icon_name));
  }
  notify_notification_add_action(notification, "app.show-updates",
                                 _("Show updates"),
                                 (NotifyActionCallback)app_show_updates,
                                 g_object_ref(self), g_object_unref);
  /* This is the default action, the one executed when the user clicks on the
   * notification itself. It has no button, so the _("Show updates") text is
   * really unnecesary. It's added just in case in a future notifications do
   * use it for... whatever... a popup, for example.
   */
  notify_notification_add_action(notification, "default", _("Show updates"),
                                 (NotifyActionCallback)app_show_updates,
                                 g_object_ref(self), g_object_unref);
  if (allow_to_ignore) {
    g_autoptr(GVariant) snap_list = get_snap_list(snaps);
    /// TRANSLATORS: Text for a button in a notification. Pressing it
    /// will inform the program to not notify again that there are
    /// refreshes for the snaps specified in the notification.
    notify_notification_add_action(
        notification, "app.ignore-notification", _("Don't remind me again"),
        (NotifyActionCallback)app_ignore_snaps_notification,
        ignore_notify_data_new(self, snap_list),
        (GFreeFunc)ignore_notify_data_free);
  }
  notify_notification_show(notification, NULL);
}

static void update_complete_notification(SdiNotify *self, const gchar *title,
                                         const gchar *body, GIcon *icon,
                                         const gchar *id,
                                         const gchar *desktop) {
  g_autofree gchar *icon_name = get_icon_name_from_gicon(icon);
  // Don't use g_autoptr because it must survive for the actions
  NotifyNotification *notification =
      notify_notification_new(title, body, icon_name);

  if (icon_name != NULL) {
    notify_notification_set_hint(notification, "image-path",
                                 g_variant_new_string(icon_name));
  }

  if (desktop == NULL) {
    /// TRANSLATORS: Text for one of the buttons in the notification shown
    /// after a snap has been refreshed. Pressing it will close the
    /// notification.
    notify_notification_add_action(notification, "default", _("Close"),
                                   (NotifyActionCallback)app_close_notification,
                                   g_object_ref(self), g_object_unref);
  } else {
    LaunchUpdatedApp *data = launch_updated_app_new(self, desktop);
    notify_notification_add_action(notification, "default", _("Close"),
                                   (NotifyActionCallback)app_launch_updated,
                                   data, launch_updated_app_free);
  }
  notify_notification_show(notification, NULL);
}

#else

static void show_pending_update_notification(SdiNotify *self,
                                             const gchar *title,
                                             const gchar *body, GIcon *icon,
                                             GListModel *snaps,
                                             gboolean allow_to_ignore) {
  g_autoptr(GNotification) notification = g_notification_new(title);
  g_notification_set_body(notification, body);
  if (icon != NULL) {
    g_notification_set_icon(notification, g_object_ref(icon));
  }
  g_notification_set_default_action_and_target(notification, "app.show-updates",
                                               "s", "pending-update");
  g_notification_add_button_with_target(notification, _("Show updates"),
                                        "app.show-updates", "s",
                                        "pending-update");
  if (allow_to_ignore) {
    g_autoptr(GVariantBuilder) builder =
        g_variant_builder_new(G_VARIANT_TYPE("as"));
    for (int i = 0; i < g_list_model_get_n_items(snaps); i++) {
      g_autoptr(SnapdSnap) snap = g_list_model_get_item(snaps, i);
      g_variant_builder_add(builder, "s", snapd_snap_get_name(snap));
    }
    GVariant *values = g_variant_ref_sink(g_variant_builder_end(builder));
    g_notification_add_button_with_target_value(
        notification, _("Don't remind me again"), "app.ignore-updates", values);
  }
  g_application_send_notification(self->application, "pending-update",
                                  notification);
}

static void update_complete_notification(SdiNotify *self, const gchar *title,
                                         const gchar *body, GIcon *icon,
                                         const gchar *id,
                                         const gchar *desktop) {
  g_autoptr(GNotification) notification = g_notification_new(title);
  g_notification_set_body(notification, body);
  if (icon != NULL) {
    g_notification_set_icon(notification, g_object_ref(icon));
  }
  if (desktop != NULL) {
    g_notification_set_default_action_and_target(
        notification, "app.launch-refreshed-app", "s", desktop);
  }
  g_application_send_notification(self->application, id, notification);
}
#endif

void sdi_notify_pending_refresh_forced(SdiNotify *self, SnapdSnap *snap,
                                       GTimeSpan remaining_time,
                                       gboolean allow_to_ignore) {
  g_return_if_fail(SDI_IS_NOTIFY(self));
  g_return_if_fail(snap != NULL);

  g_autoptr(GAppInfo) app_info = sdi_get_desktop_file_from_snap(snap);

  const gchar *name = snapd_snap_get_name(snap);
  if (app_info != NULL) {
    name = g_app_info_get_display_name(app_info);
  }

  g_autofree gchar *title = NULL;

  if (remaining_time > SECONDS_IN_A_DAY) {
    /// TRANSLATORS: The %s is the name of a snap that is currently running,
    /// and it will be closed and updated in %ld days if the user doesn't
    /// close it before. This is shown after the user has been notified several
    /// times that there is a refresh available for a running snap, but they
    /// hasn't closed it, to inform they that there is a time limit before the
    /// snap is forced to quit to refresh it.
    title = g_strdup_printf(_("%s will quit and update in %ld days"), name,
                            remaining_time / SECONDS_IN_A_DAY);
  } else if (remaining_time > SECONDS_IN_AN_HOUR) {
    /// TRANSLATORS: The %s is the name of a snap that is currently running,
    /// and it will be closed and updated in %ld hours if the user doesn't
    /// close it before.
    title = g_strdup_printf(_("%s will quit and update in %ld hours"), name,
                            remaining_time / SECONDS_IN_AN_HOUR);
  } else {
    /// TRANSLATORS: The %s is the name of a snap that is currently running,
    /// and it will be closed and updated in %ld minutes if the user doesn't
    /// close it before.
    title = g_strdup_printf(_("%s will quit and update in %ld minutes"), name,
                            remaining_time / SECONDS_IN_A_MINUTE);
  }

  GIcon *icon = NULL;
  if (app_info != NULL) {
    icon = g_app_info_get_icon(app_info);
  }

  g_autoptr(GListStore) snap_list = g_list_store_new(SNAPD_TYPE_SNAP);
  g_list_store_append(snap_list, snap);
  /// TRANSLATORS: This message is shown below the "%s will quit and update
  /// in..." message.
  show_pending_update_notification(
      self, title, _("Save your progress and quit now to prevent data loss."),
      icon, G_LIST_MODEL(snap_list), allow_to_ignore);
}

static gchar *get_name_from_snap(SnapdSnap *snap) {
  const gchar *name = snapd_snap_get_name(snap);
  g_autoptr(GAppInfo) app_info = sdi_get_desktop_file_from_snap(snap);
  if (app_info != NULL) {
    const gchar *name2 = g_app_info_get_display_name(app_info);
    if (name2 != NULL) {
      name = name2;
    }
  }
  return g_strdup(name);
}

static gchar *build_body_message_for_two_refreshes(SnapdSnap *snap0,
                                                   SnapdSnap *snap1) {
  g_autofree gchar *snap_name0 = get_name_from_snap(snap0);
  g_autofree gchar *snap_name1 = get_name_from_snap(snap1);
  /// TRANSLATORS: This message is used when there are two pending
  /// refreshes.
  return g_strdup_printf(_("%s and %s will update when you quit them."),
                         snap_name0, snap_name1);
}

static gchar *build_body_message_for_three_refreshes(SnapdSnap *snap0,
                                                     SnapdSnap *snap1,
                                                     SnapdSnap *snap2) {
  g_autofree gchar *snap_name0 = get_name_from_snap(snap0);
  g_autofree gchar *snap_name1 = get_name_from_snap(snap1);
  g_autofree gchar *snap_name2 = get_name_from_snap(snap2);
  /// TRANSLATORS: This message is used when there are three pending
  /// refreshes.
  return g_strdup_printf(_("%s, %s and %s will update when you quit them."),
                         snap_name0, snap_name1, snap_name2);
}

void sdi_notify_pending_refresh(SdiNotify *self, GListModel *snaps) {
  g_return_if_fail(SDI_IS_NOTIFY(self));
  g_return_if_fail(snaps != NULL);

  g_autofree gchar *title = NULL;
  g_autofree gchar *body = NULL;
  g_autoptr(GAppInfo) app_info = NULL;
  g_autoptr(GDesktopAppInfo) app_info2 = NULL;

  guint n_snaps = g_list_model_get_n_items(snaps);

  GIcon *icon = NULL;
  if (n_snaps == 1) {
    g_autoptr(SnapdSnap) snap0 = g_list_model_get_item(snaps, 0);
    g_autofree gchar *snap_name = get_name_from_snap(snap0);
    /// TRANSLATORS: The %s is the name of a snap that has an update available.
    title = g_strdup_printf(_("Update available for %s"), snap_name);
    body = g_strdup(_("Quit the app to update it now."));
    app_info = sdi_get_desktop_file_from_snap(snap0);
    if (app_info != NULL) {
      icon = g_app_info_get_icon(app_info);
    }
  } else {
    /* Although the case for 1 app is managed outside this, I put it here to
     * ensure that ngettext works as expected, and to ensure that translators
     * know what is going on.
     */
    /// TRANSLATORS: when there are several updates available, this is the
    /// message used to notify the user how many updates are.
    title = g_strdup_printf(ngettext("Update available for %d app",
                                     "Updates available for %d apps", n_snaps),
                            n_snaps);
    g_autoptr(SnapdSnap) snap0 = g_list_model_get_item(snaps, 0);
    g_autoptr(SnapdSnap) snap1 = g_list_model_get_item(snaps, 1);
    /* snap2 will be NULL if the number of items is less than three,
     * so it's not a problem to do this.
     */
    g_autoptr(SnapdSnap) snap2 = g_list_model_get_item(snaps, 2);
    switch (n_snaps) {
    case 2:
      body = build_body_message_for_two_refreshes(snap0, snap1);
      break;
    case 3:
      body = build_body_message_for_three_refreshes(snap0, snap1, snap2);
      break;
    default:
      /// TRANSLATORS: This message is used when there are four or more pending
      /// refreshes.
      body = g_strdup(_("Quit the apps to update them now."));
      break;
    }
  }
  if (icon == NULL) {
    app_info2 = g_desktop_app_info_new(SNAP_STORE);
    if (app_info2 != NULL) {
      icon = g_app_info_get_icon(G_APP_INFO(app_info2));
    }
  }
  show_pending_update_notification(self, title, body, icon, snaps, TRUE);
}

void sdi_notify_refresh_complete(SdiNotify *self, SnapdSnap *snap,
                                 const gchar *snap_name) {
  g_return_if_fail(SDI_IS_NOTIFY(self));
  g_return_if_fail((snap != NULL) || (snap_name != NULL));

  GIcon *icon = NULL;
  g_autoptr(GAppInfo) app_info = NULL;
  const gchar *name = NULL;
  const gchar *desktop = NULL;

  if (snap != NULL) {
    app_info = sdi_get_desktop_file_from_snap(snap);
    if (app_info != NULL) {
      name = g_app_info_get_display_name(app_info);
      icon = g_app_info_get_icon(app_info);
      desktop = g_desktop_app_info_get_filename(G_DESKTOP_APP_INFO(app_info));
    }
    if (name == NULL) {
      name = snapd_snap_get_name(snap);
    }
  }
  if (name == NULL) {
    name = snap_name;
  }

  g_autofree gchar *title = g_strdup_printf(_("%s was updated"), name);

  update_complete_notification(self, title, _("You can reopen it now."), icon,
                               "update-complete", desktop);
}

static void set_actions(SdiNotify *self) {
  g_autoptr(GVariantType) type_ignore = g_variant_type_new("as");
  g_autoptr(GSimpleAction) action_ignore =
      g_simple_action_new("ignore-updates", type_ignore);
  g_action_map_add_action(G_ACTION_MAP(self->application),
                          G_ACTION(action_ignore));
  g_autoptr(GVariantType) type_close = g_variant_type_new("s");
  g_autoptr(GSimpleAction) action_close =
      g_simple_action_new("close-notification", type_close);
  g_autoptr(GVariantType) type_updates = g_variant_type_new("s");
  g_action_map_add_action(G_ACTION_MAP(self->application),
                          G_ACTION(action_close));
  g_autoptr(GSimpleAction) action_show_updates =
      g_simple_action_new("show-updates", type_updates);
  g_action_map_add_action(G_ACTION_MAP(self->application),
                          G_ACTION(action_show_updates));
  g_signal_connect(G_OBJECT(action_ignore), "activate",
                   (GCallback)sdi_notify_action_ignore, self);
  g_signal_connect(G_OBJECT(action_show_updates), "activate",
                   (GCallback)sdi_notify_action_show_updates, self);
}

static void sdi_notify_set_property(GObject *object, guint prop_id,
                                    const GValue *value, GParamSpec *pspec) {
  SdiNotify *self = SDI_NOTIFY(object);
  gpointer p = NULL;

  switch (prop_id) {
  case PROP_APPLICATION:
    g_clear_object(&self->application);
    p = g_value_get_object(value);
    if (p != NULL) {
      self->application = g_object_ref(p);
      set_actions(self);
    }
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void sdi_notify_get_property(GObject *object, guint prop_id,
                                    GValue *value, GParamSpec *pspec) {
  SdiNotify *self = SDI_NOTIFY(object);

  switch (prop_id) {
  case PROP_APPLICATION:
    g_value_set_object(value, self->application);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void sdi_notify_dispose(GObject *object) {
  SdiNotify *self = SDI_NOTIFY(object);

  g_clear_object(&self->application);

  G_OBJECT_CLASS(sdi_notify_parent_class)->dispose(object);
}

void sdi_notify_init(SdiNotify *self) {
#ifndef USE_GNOTIFY
  notify_init("snapd-desktop-integration-test_snapd-desktop-integration-test");
#endif
}

void sdi_notify_class_init(SdiNotifyClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

  gobject_class->set_property = sdi_notify_set_property;
  gobject_class->get_property = sdi_notify_get_property;
  gobject_class->dispose = sdi_notify_dispose;

  g_object_class_install_property(
      gobject_class, PROP_APPLICATION,
      g_param_spec_object("application", "application",
                          "GApplication associated with this notify",
                          G_TYPE_APPLICATION,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  g_signal_new("ignore-snap-event", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
               0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
#ifdef DEBUG_TESTS
  g_signal_new("notification-closed", G_TYPE_FROM_CLASS(klass),
               G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1,
               G_TYPE_STRING);
#endif
}

SdiNotify *sdi_notify_new(GApplication *application) {
  g_return_val_if_fail(application != NULL, NULL);

  return g_object_new(SDI_TYPE_NOTIFY, "application", application, NULL);
}
