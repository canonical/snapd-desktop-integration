/*
 * Copyright (C) 2025 Canonical Ltd
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

/*
 * This is a helper used to launch snapped user daemons that must run
 * only when a graphical session is active.
 *
 * It does it by checking the current sessions from GDM, and waiting
 * until a graphical session is detected, in which case it will wait
 * until one of a series of DBus well-known names do appear in the
 * session bus to ensure that the session has really started.
 *
 * If it has to wait in any of these steps, it will exit directly, so
 * snapd will have to relaunch it. Only when all these steps are correct
 * from the beginning will this daemon use execve to replace its code
 * with the desired executable, passed on the parameters list.
 *
 * This helper must be run as a snap user daemon, with `restart-condition:
 * always` and `restart-delay: 1` at least. Also, it requires the `unity7` and
 * the `login-session-observe` plugs to be able to access the required
 * DBus objects.
 */

#define _GNU_SOURCE

#include "sdi-user-session-helper.h"
#include <unistd.h>

// wait up to ten seconds before timing out for DBus name appearing
#define MAX_WAIT 10
#define NAME_TO_WATCH "org.freedesktop.portal.Desktop"

static gboolean sdi_session_is_desktop(const gchar *object_path) {
  g_autoptr(OrgFreedesktopLogin1Session) session = NULL;
  guint32 user;
  // these values belongs to the session proxy, so they must not be freed
  GVariant *user_data = NULL;
  const gchar *session_type = NULL;

  session = org_freedesktop_login1_session_proxy_new_for_bus_sync(
      G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, "org.freedesktop.login1",
      object_path, NULL, NULL);
  user_data = org_freedesktop_login1_session_get_user(session);
  if (user_data == NULL) {
    g_warning("Failed to read the session user data. Forcing a reload.");
    /* if we can't read the data, we can't know whether we are in a desktop
     * session or in a text one, so we will assume that we are in a session
     * desktop to force a reload.
     */
    return TRUE;
  }
  g_variant_get(user_data, "(uo)", &user, NULL);
  if (getuid() != user) {
    // the new session isn't for our user
    return FALSE;
  }
  session_type = org_freedesktop_login1_session_get_type_(session);
  if (session_type == NULL) {
    g_warning("Failed to read the session type");
    return FALSE;
  }
  if (g_str_equal("x11", session_type) ||
      g_str_equal("wayland", session_type) ||
      g_str_equal("mir", session_type)) {
    // this is a graphical session
    return TRUE;
  }
  return FALSE;
}

static gboolean sdi_check_graphical_sessions(Login1Manager *login_manager) {
  g_autoptr(GVariant) sessions = NULL;
  gboolean got_session_list;

  got_session_list = login1_manager_call_list_sessions_sync(
      login_manager, &sessions, NULL, NULL);

  if (!got_session_list) {
    g_warning("Failed to get session list (check that login-session-observe "
              "interface is connected). Forcing a reload.");
    return false;
  }

  /* check if there is already a graphical session opened for us, in which
   * case we must just exit and let systemd to relaunch us, because it means
   * that we run too early and the desktop wasn't still ready.
   */
  for (int i = 0; i < g_variant_n_children(sessions); i++) {
    g_autoptr(GVariant) session = g_variant_get_child_value(sessions, i);
    if (session == NULL) {
      continue;
    }
    g_autofree gchar *session_object = NULL;
    g_variant_get(session, "(susso)", NULL, NULL, NULL, NULL, &session_object);
    g_debug("Checking session %s...", session_object);
    if (sdi_session_is_desktop(session_object)) {
      g_debug("We are already in a desktop session!");
      return true;
    }
  }
  return false;
}

static void new_session(Login1Manager *manager, const gchar *session_id,
                        const gchar *object_path, GMainLoop *loop) {
  g_debug("Detected new session %s at %s", session_id, object_path);

  if (sdi_session_is_desktop(object_path)) {
    g_debug("The new session is of desktop type. Relaunching "
            "snapd-desktop-integration.");
    g_main_loop_quit(loop);
  }
}

static void name_appeared(GDBusConnection *connection,
                          const gchar *name,
                          const gchar *name_owner,
                          gpointer user_data) {

  GMainLoop *loop = user_data;

  g_debug("Name '%s' appeared (owner: %s)", name, name_owner);
  g_main_loop_quit(loop);
}

/**
 * Returns TRUE if we are already in a fully functional graphical session,
 * or FALSE if, when it entered, there was no functional graphical session
 * but it become while waiting. Thus, if it returns TRUE, the caller can
 * continue to initialize GTK and do their things, while if it's FALSE, it
 * must exit and let systemd reload it again, to have its environment
 * updated. In that reload, this function will return TRUE.
 */
bool sdi_wait_for_graphical_session(void) {
  bool already_in_graphical_session = true;
  g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, TRUE);
  g_autoptr(Login1Manager) login_manager =
      login1_manager_proxy_new_for_bus_sync(
          G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, "org.freedesktop.login1",
          "/org/freedesktop/login1", NULL, NULL);
  gulong session_new_id = g_signal_connect(login_manager, "session-new",
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
  if (!sdi_check_graphical_sessions(login_manager)) {
    already_in_graphical_session = false;
    g_main_loop_run(loop);
  }
  g_clear_signal_handler(&session_new_id, login_manager);

  /* Now we know that we are in a graphical session, so now wait for
   * the DBus well-known name NAME_TO_WATCH to be acquired by a process, to
   * guarantee that the system is ready
   */

  g_timeout_add_once(MAX_WAIT * 1000, (GSourceOnceFunc) g_main_loop_quit, loop);

  guint watcher_id = g_bus_watch_name(G_BUS_TYPE_SESSION,
                                      NAME_TO_WATCH,
                                      G_BUS_NAME_WATCHER_FLAGS_NONE,
                                      name_appeared,
                                      NULL,
                                      loop,
                                      NULL);

  g_debug("Waiting for D-Bus name '%s'...", NAME_TO_WATCH);
  g_main_loop_run(loop);

  g_bus_unwatch_name(watcher_id);
  return already_in_graphical_session;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    g_error("At least one parameter must be passed. Aborting.");
    return EXIT_FAILURE;
  }
  if (!sdi_wait_for_graphical_session()) {
    g_debug("New graphical session opened. Forcing reload.");
    return EXIT_SUCCESS;
  }
  g_debug("Fully initialized graphical session detected. Launching daemon.");
  execve(argv[1], argv + 1, environ);
  return EXIT_SUCCESS; // just to avoid warnings in compiler
}
