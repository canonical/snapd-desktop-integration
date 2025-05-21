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

#include "sdi-user-session-helper.h"

static void properties_changed(GDBusProxy *self, gchar *v,
                               GVariant *changed_properties,
                               char **invalidated_properties, GMainLoop *loop) {
  if (changed_properties == NULL) {
    return;
  }
  if (!g_variant_is_container(changed_properties)) {
    return;
  }
  for (int i = 0; i < g_variant_n_children(changed_properties); i++) {
    g_autoptr(GVariant) property =
        g_variant_get_child_value(changed_properties, i);
    if (property == NULL) {
      continue;
    }
    g_autoptr(GVariant) property_name_v =
        g_variant_get_child_value(property, 0);
    const gchar *property_name = g_variant_get_string(property_name_v, NULL);
    g_debug("Detected property change in graphical-target: %s", property_name);
    if (0 != g_strcmp0(property_name, "ActiveState")) {
      continue;
    }
    g_autoptr(GVariant) property_state_child =
        g_variant_get_child_value(property, 1);
    g_autoptr(GVariant) property_state_v =
        g_variant_get_child_value(property_state_child, 0);
    const gchar *property_state = g_variant_get_string(property_state_v, NULL);
    g_debug("ActiveState changed to: %s", property_state);
    if (0 == g_strcmp0(property_state, "active")) {
      g_main_loop_quit(loop);
      break;
    }
  }
}

bool sdi_wait_for_graphical_session(void) {
  g_autoptr(GError) error = NULL;
  g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, TRUE);

  g_autoptr(Systemd1Manager) systemd_manager =
      systemd1_manager__proxy_new_for_bus_sync(
          G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE,
          "org.freedesktop.systemd1", "/org/freedesktop/systemd1", NULL,
          &error);

  if (error != NULL) {
    g_warning("Error when asking for systemd1 manager: %s", error->message);
    return false;
  }
  g_autofree gchar *graphical_session_target_object = NULL;
  systemd1_manager__call_get_unit_sync(
      systemd_manager, "graphical-session.target",
      &graphical_session_target_object, NULL, NULL);

  if (error != NULL) {
    g_warning("Error when asking for the target object: %s", error->message);
    return false;
  }

  g_autoptr(GraphicalSessionTargetOrgFreedesktopDBusProperties)
      graphical_target_properties =
          graphical_session_target_org_freedesktop_dbus_properties_proxy_new_for_bus_sync(
              G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE,
              "org.freedesktop.systemd1", graphical_session_target_object, NULL,
              &error);
  if (error != NULL) {
    g_warning(
        "Error when getting the graphical-session.target properties object: %s",
        error->message);
    return false;
  }
  g_autoptr(GraphicalSessionTarget) graphical_target =
      graphical_session_target__proxy_new_for_bus_sync(
          G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE,
          "org.freedesktop.systemd1", graphical_session_target_object, NULL,
          &error);
  if (error != NULL) {
    g_warning("Error when getting the graphical-session.target object: %s",
              error->message);
    return false;
  }

  guint session_new_id =
      g_signal_connect(graphical_target_properties, "properties-changed",
                       G_CALLBACK(properties_changed), loop);

  const gchar *current_status =
      graphical_session_target__get_active_state(graphical_target);
  g_debug("Current status: %s", current_status);
  if (0 != g_strcmp0(current_status, "active")) {
    // if the status is not 'active', wait for a notification that it has
    // changed
    g_main_loop_run(loop);
  }
  g_signal_handler_disconnect(graphical_target_properties, session_new_id);
  return true;
}
