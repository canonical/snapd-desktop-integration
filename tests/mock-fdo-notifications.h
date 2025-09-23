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

#pragma once

#include <gio/gio.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MOCK_FDO_TYPE_NOTIFICATIONS mock_fdo_notifications_get_type()

typedef struct {
  const gchar *app_name;
  guint32 replaces_id;
  const gchar *title;
  const gchar *body;
  const gchar *icon;
  const gchar *icon_path;
  GStrv actions;
  GVariant *hints;
  guint32 expire_timeout;
  guint32 uid;
} MockNotificationsData;

G_DECLARE_FINAL_TYPE(MockFdoNotifications, mock_fdo_notifications, MOCK_FDO,
                     NOTIFICATIONS, GObject)

MockFdoNotifications *mock_fdo_notifications_new(void);

gboolean mock_fdo_notifications_setup_session_bus(GError **error);

void mock_fdo_notifications_run(MockFdoNotifications *mock, int argc,
                                char **argv);
void mock_fdo_notifications_quit(MockFdoNotifications *self);

MockNotificationsData *
mock_fdo_notifications_wait_for_notification(MockFdoNotifications *mock,
                                             guint timeout);

void mock_fdo_notifications_send_action(MockFdoNotifications *mock, guint32 uid,
                                        gchar *action);

G_END_DECLS
