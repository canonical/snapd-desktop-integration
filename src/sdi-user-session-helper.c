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

static Login1Manager *login_manager = NULL;
static guint idle_id = 0;

static gboolean sdi_session_is_desktop(const gchar *object_path) {
  g_autoptr(OrgFreedesktopLogin1Session) session = NULL;
  g_autoptr(GVariant) user = NULL;
  // these values belongs to the session proxy, so they must not be freed
  GVariant *user_data = NULL;
  const gchar *session_type = NULL;

  session = org_freedesktop_login1_session_proxy_new_for_bus_sync(
      G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, "org.freedesktop.login1",
      object_path, NULL, NULL);
  user_data = org_freedesktop_login1_session_get_user(session);
  if (user_data == NULL) {
    g_message("Failed to read the session user data. Forcing a reload.");
    /* if we can't read the data, we can't know whether we are in a desktop
     * session or in a text one, so we will assume that we are in a session
     * desktop to force a reload.
     */
    return TRUE;
  }
  user = g_variant_get_child_value(user_data, 0);
  if (user == NULL) {
    g_message("Failed to read the session user.");
    return FALSE;
  }
  if (getuid() != g_variant_get_uint32(user)) {
    // the new session isn't for our user
    return FALSE;
  }
  session_type = org_freedesktop_login1_session_get_type_(session);
  if (session_type == NULL) {
    g_message("Failed to read the session type");
    return FALSE;
  }
  if (!g_strcmp0("x11", session_type) || !g_strcmp0("wayland", session_type) ||
      !g_strcmp0("mir", session_type)) {
    // this is a graphical session
    return TRUE;
  }
  return FALSE;
}

static gboolean sdi_check_graphical_sessions(GMainLoop *loop) {
  GVariant *sessions = NULL;
  gboolean got_session_list;

  got_session_list = login1_manager_call_list_sessions_sync(
      login_manager, &sessions, NULL, NULL);

  if (got_session_list) {
    /* check if there is already a graphical session opened for us, in which
     * case we must just exit and let systemd to relaunch us, because it means
     * that we run too early and the desktop wasn't still ready.
     */
    for (int i = 0; i < g_variant_n_children(sessions); i++) {
      GVariant *session = g_variant_get_child_value(sessions, i);
      if (session == NULL) {
        continue;
      }
      GVariant *session_object_variant = g_variant_get_child_value(session, 4);
      const gchar *session_object =
          g_variant_get_string(session_object_variant, NULL);
      g_message("Checking session %s...", session_object);
      if (sdi_session_is_desktop(session_object)) {
        g_message("Is a desktop session! Forcing a reload.");
        g_main_loop_quit(loop);
      }
      g_variant_unref(session_object_variant);
      g_variant_unref(session);
    }
  } else {
    g_message("Failed to get session list (check that login-session-observe "
              "interface is connected). Forcing a reload.");
    g_main_loop_quit(loop);
  }
  idle_id = 0; /* we are already removing it here, so g_source_remove should not
                * be called
                */
  return G_SOURCE_REMOVE;
}

static void new_session(Login1Manager *manager, const gchar *session_id,
                        const gchar *object_path, GMainLoop *loop) {
  g_message("Detected new session %s at %s\n", session_id, object_path);

  if (sdi_session_is_desktop(object_path)) {
    g_message("The new session is of desktop type. Relaunching "
              "snapd-desktop-integration.");
    g_main_loop_quit(loop);
  }
}

void sdi_wait_for_graphical_session(void) {
  g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, TRUE);
  login_manager = login1_manager_proxy_new_for_bus_sync(
      G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, "org.freedesktop.login1",
      "/org/freedesktop/login1", NULL, NULL);
  guint session_new_id = g_signal_connect(login_manager, "session-new",
                                          G_CALLBACK(new_session), loop);
  /* Check if we are already in a graphical session to avoid race conditions
   * between the signals being connected and the main loop being run. This is
   * a must because, sometimes, snapd-desktop-integration is launched "too
   * quickly" and the desktop isn't ready, so gtk_init_check() fails but the
   * session IS a graphical one. For that cases we do check if there is a
   * graphical session active, and if that's the case, we must exit to let
   * systemd relaunch us again, this time being able to get access to the
   * session.
   */
  idle_id = g_idle_add((GSourceFunc)sdi_check_graphical_sessions, loop);
  g_main_loop_run(loop);
  g_signal_handler_disconnect(login_manager, session_new_id);
  if (idle_id != 0) {
    g_source_remove(idle_id);
  }
  /* login_manager is used in _sdi_check_graphical_session, so we can't make it
   * local and use g_autoptr
   */
  g_clear_object(&login_manager);
}
