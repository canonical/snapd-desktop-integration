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

#include "config.h"
#include <errno.h>
#include <glib-unix.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libnotify/notify.h>
#include <locale.h>
#include <signal.h>
#include <snapd-glib/snapd-glib.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

#include "sdi-notify.h"
#include "sdi-refresh-monitor.h"
#include "sdi-theme-monitor.h"
#include "sdi-user-session-helper.h"

static SnapdClient *client = NULL;
static SdiThemeMonitor *theme_monitor = NULL;
static SdiRefreshMonitor *refresh_monitor = NULL;
static SdiNotify *notify_manager = NULL;

static gchar *snapd_socket_path = NULL;

static GOptionEntry entries[] = {{"snapd-socket-path", 0, 0,
                                  G_OPTION_ARG_FILENAME, &snapd_socket_path,
                                  "Snapd socket path", "PATH"},
                                 {NULL}};

static void do_startup(GObject *object, gpointer data) {
  GError *error = NULL;
  client = snapd_client_new();
  refresh_monitor = sdi_refresh_monitor_new(G_APPLICATION(object));
  notify_manager = sdi_notify_new(G_APPLICATION(object));
  g_signal_connect_object(refresh_monitor, "notify-pending-refresh",
                          (GCallback)sdi_notify_pending_refresh, notify_manager,
                          G_CONNECT_SWAPPED);
  g_signal_connect_object(refresh_monitor, "notify-pending-refresh-forced",
                          (GCallback)sdi_notify_pending_refresh_forced,
                          notify_manager, G_CONNECT_SWAPPED);
  g_signal_connect_object(refresh_monitor, "notify-refresh-complete",
                          (GCallback)sdi_notify_refresh_complete,
                          notify_manager, G_CONNECT_SWAPPED);
  g_signal_connect_object(notify_manager, "ignore-snap-event",
                          (GCallback)sdi_refresh_monitor_ignore_snap_cb,
                          refresh_monitor, G_CONNECT_SWAPPED);
  if (!sdi_refresh_monitor_start(refresh_monitor, &error)) {
    g_message("Failed to export the DBus Desktop Integration API %s",
              error->message);
  }
}

static void do_activate(GObject *object, gpointer data) {
  // because, by default, there are no windows, so the application would quit
  g_application_hold(G_APPLICATION(object));

  if (snapd_socket_path != NULL) {
    snapd_client_set_socket_path(client, snapd_socket_path);
  } else if (g_getenv("SNAP") != NULL) {
    snapd_client_set_socket_path(client, "/run/snapd-snap.socket");
  }

  theme_monitor = sdi_theme_monitor_new(client);
  sdi_theme_monitor_start(theme_monitor);
}

static void do_shutdown(GObject *object, gpointer data) {
  notify_uninit();
  g_clear_object(&client);
  g_clear_object(&theme_monitor);
  g_clear_object(&refresh_monitor);
  g_clear_object(&login_manager);
  g_clear_object(&notify_manager);
}

static int global_retval = 0;

void sighandler(int v) {
  global_retval = 128 + v; // exit value is usually 128 + signal_id
}

static gboolean close_app(GApplication *application) {
  g_application_quit(application);
  return G_SOURCE_REMOVE;
}

int main(int argc, char **argv) {
  int retval;
  int pid = fork();
  if (pid != 0) {
    if (pid > 0) {
      // SIGTERM and SIGINT will be ignored, but
      // will break WAITPID with a EINTR value in
      // errno.
      // This allows the program to always exit with
      // a NO-ERROR value, and kill the child
      struct sigaction signal_data;
      signal_data.sa_handler = sighandler;
      sigemptyset(&signal_data.sa_mask);
      signal_data.sa_flags = 0;
      sigaction(SIGTERM, &signal_data, NULL);
      sigaction(SIGINT, &signal_data, NULL);

      retval = waitpid(pid, NULL, 0);
      if ((retval < 0) && (errno == EINTR)) {
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
      }
    }
    return global_retval;
  }

  setlocale(LC_ALL, "");
  bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
  textdomain(GETTEXT_PACKAGE);

  if (!gtk_init_check()) {
    g_message("Failed to do gtk init. Waiting for a new session with desktop "
              "capabilities.");
    sdi_wait_for_graphical_session();
    g_message("Loop exited. Forcing reload.");
    return 0;
  }

  g_autoptr(GtkApplication) app = gtk_application_new(
      "io.snapcraft.SnapDesktopIntegration",
      G_APPLICATION_ALLOW_REPLACEMENT | G_APPLICATION_REPLACE);
  g_signal_connect(G_OBJECT(app), "startup", G_CALLBACK(do_startup), NULL);
  g_signal_connect(G_OBJECT(app), "shutdown", G_CALLBACK(do_shutdown), NULL);
  g_signal_connect(G_OBJECT(app), "activate", G_CALLBACK(do_activate), NULL);

  g_application_add_main_option_entries(G_APPLICATION(app), entries);

  g_unix_signal_add(SIGINT, (GSourceFunc)close_app, app);
  g_unix_signal_add(SIGTERM, (GSourceFunc)close_app, app);

  g_application_run(G_APPLICATION(app), argc, argv);

  // since it should never ends, if we reach here, we return 0 as error value to
  // ensure that systemd will relaunch it.
  return 0;
}
