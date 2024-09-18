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
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libnotify/notify.h>
#include <locale.h>
#include <signal.h>
#include <snapd-glib/snapd-glib.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

#include "sdi-refresh-monitor.h"
#include "sdi-session-helpers.h"
#include "sdi-theme-monitor.h"

static gchar *snapd_socket_path = NULL;

static GOptionEntry entries[] = {{"snapd-socket-path", 0, 0,
                                  G_OPTION_ARG_FILENAME, &snapd_socket_path,
                                  "Snapd socket path", "PATH"},
                                 {NULL}};

static SnapdClient *create_snapd_client() {
  SnapdClient *client = snapd_client_new();
  if (snapd_socket_path != NULL) {
    snapd_client_set_socket_path(client, snapd_socket_path);
  } else if (g_getenv("SNAP") != NULL) {
    snapd_client_set_socket_path(client, "/run/snapd-snap.socket");
  }
  return client;
}

static void do_startup(GObject *object, gpointer data) {
  GError *error = NULL;
#ifndef USE_GNOTIFY
  notify_init("snapd-desktop-integration_snapd-desktop-integration");
#endif
  SdiRefreshMonitor *refresh_monitor =
      sdi_refresh_monitor_new(G_APPLICATION(object));
  if (!sdi_refresh_monitor_start(refresh_monitor, &error)) {
    g_message("Failed to export the DBus Desktop Integration API %s",
              error->message);
  }
}

static void do_activate(GObject *object, gpointer data) {
  // because, by default, there are no windows, so the application would quit
  g_application_hold(G_APPLICATION(object));

  SnapdClient *client = create_snapd_client();
  SdiThemeMonitor *theme_monitor = sdi_theme_monitor_new(client);
  sdi_theme_monitor_start(theme_monitor);
}

static void do_shutdown(GObject *object, gpointer data) { notify_uninit(); }

static int global_retval = 0;

void sighandler(int v) {
  global_retval = 128 + v; // exit value is usually 128 + signal_id
}

int main(int argc, char **argv) {
  int retval;

  // This is a hack to avoid the daemon being relaunched over and
  // over again when opening a non-graphical session.
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
    // The program is running in a non-graphical session (like a SSH remote
    // session, or a text-mode terminal), so it has to wait until a graphical
    // session is opened.
    g_message("Failed to do gtk init. Waiting for a new session with desktop "
              "capabilities.");
    sdi_wait_for_graphical_session();
    g_message("Loop exited. Forcing reload.");
    return 0;
  }

  GtkApplication *app = gtk_application_new(
      "io.snapcraft.SnapDesktopIntegration",
      G_APPLICATION_ALLOW_REPLACEMENT | G_APPLICATION_REPLACE);
  g_signal_connect(G_OBJECT(app), "startup", G_CALLBACK(do_startup), NULL);
  g_signal_connect(G_OBJECT(app), "shutdown", G_CALLBACK(do_shutdown), NULL);
  g_signal_connect(G_OBJECT(app), "activate", G_CALLBACK(do_activate), NULL);

  g_application_add_main_option_entries(G_APPLICATION(app), entries);

  g_application_run(G_APPLICATION(app), argc, argv);

  // since it should never ends, if we reach here, we return 0 as error value to
  // ensure that systemd will relaunch it.
  return 0;
}
