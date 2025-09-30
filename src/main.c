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
#include "sdi-progress-dock.h"
#include "sdi-progress-window.h"
#include "sdi-refresh-monitor.h"
#include "sdi-snapd-client-factory.h"
#include "sdi-snapd-monitor.h"
#include "sdi-theme-monitor.h"

static SnapdClient *client = NULL;
static SdiThemeMonitor *theme_monitor = NULL;
static SdiRefreshMonitor *refresh_monitor = NULL;
static SdiNotify *notify_manager = NULL;
static SdiSnapdMonitor *snapd_monitor = NULL;
static SdiProgressWindow *progress_window = NULL;
static SdiProgressDock *progress_dock = NULL;

static gchar *snapd_socket_path = NULL;

static GOptionEntry entries[] = {{"snapd-socket-path", 0, G_OPTION_FLAG_NONE,
                                  G_OPTION_ARG_FILENAME, &snapd_socket_path,
                                  "Snapd socket path", "PATH"},
                                 {NULL}};

static void do_startup(GObject *object, gpointer data) {
  sdi_snapd_client_factory_set_custom_path(snapd_socket_path);

  refresh_monitor = sdi_refresh_monitor_new();

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
                          (GCallback)sdi_refresh_monitor_ignore_snap,
                          refresh_monitor, G_CONNECT_SWAPPED);

  snapd_monitor = sdi_snapd_monitor_new();
  /* any notice event received by the #sdi_snapd_monitor object will
   * be relayed directly to the #sdi_refresh_monitor, which will process
   * them and decide whether to show a progress bar, a notification, a
   * dialog with the current progress...
   */
  g_signal_connect_object(snapd_monitor, "notice-event",
                          (GCallback)sdi_refresh_monitor_notice,
                          refresh_monitor, G_CONNECT_SWAPPED);

  progress_window = sdi_progress_window_new(G_APPLICATION(object));
  g_signal_connect_object(refresh_monitor, "begin-refresh",
                          (GCallback)sdi_progress_window_begin_refresh,
                          progress_window, G_CONNECT_SWAPPED);
  g_signal_connect_object(refresh_monitor, "refresh-progress",
                          (GCallback)sdi_progress_window_update_progress,
                          progress_window, G_CONNECT_SWAPPED);
  g_signal_connect_object(refresh_monitor, "end-refresh",
                          (GCallback)sdi_progress_window_end_refresh,
                          progress_window, G_CONNECT_SWAPPED);

  progress_dock = sdi_progress_dock_new(G_APPLICATION(object));
  g_signal_connect_object(refresh_monitor, "refresh-progress",
                          (GCallback)sdi_progress_dock_update_progress,
                          progress_dock, G_CONNECT_SWAPPED);

  if (!sdi_snapd_monitor_start(snapd_monitor)) {
    g_message("Failed to start monitor");
  }
}

static void do_activate(GObject *object, gpointer data) {
  // because, by default, there are no windows, so the application would quit
  g_application_hold(G_APPLICATION(object));
  client = sdi_snapd_client_factory_new_snapd_client();

  theme_monitor = sdi_theme_monitor_new(client);
  sdi_theme_monitor_start(theme_monitor);
}

static void do_shutdown(GObject *object, gpointer data) {
  notify_uninit();
  g_clear_object(&client);
  g_clear_object(&theme_monitor);
  g_clear_object(&refresh_monitor);
  g_clear_object(&progress_window);
  g_clear_object(&progress_dock);
  g_clear_object(&notify_manager);
  g_clear_object(&snapd_monitor);
}

static gboolean close_app(GApplication *application) {
  g_application_quit(application);
  return G_SOURCE_REMOVE;
}

int main(int argc, char **argv) {
  setlocale(LC_ALL, "");
  bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
  textdomain(GETTEXT_PACKAGE);

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

  return 0;
}
