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

#include <gio/gio.h>
#define G_SETTINGS_ENABLE_BACKEND
#include <gio/gsettingsbackend.h>
#include <gio/gunixsocketaddress.h>
#include <glib-unix.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

#include "config.h"

static GMainLoop *loop = NULL;
static gchar *temp_dir = NULL;
static gchar *snapd_socket_path = NULL;
static SoupServer *snapd_server = NULL;
static GSubprocess *dbus_subprocess = NULL;
static gchar *dbus_address = NULL;
static guint32 next_notification_id = 1;
static int exit_code = EXIT_FAILURE;

enum {
  STATE_GET_EXISTING_THEME_STATUS,
  STATE_GET_NEW_THEME_STATUS,
  STATE_PROMPT_INSTALL,
  STATE_INSTALL_THEMES,
  STATE_NOTIFY_COMPLETE
} state = STATE_GET_EXISTING_THEME_STATUS;

// TICS -DEADCODE: it's a test
static gchar *get_json(SoupMessageBody *message_body) {
  g_autoptr(JsonParser) parser = json_parser_new();
  if (!json_parser_load_from_data(parser, message_body->data,
                                  message_body->length, NULL)) {
    return NULL;
  }

  g_autoptr(JsonGenerator) generator = json_generator_new();
  json_generator_set_root(generator, json_parser_get_root(parser));

  return json_generator_to_data(generator, NULL);
}

// TICS -DEADCODE: it's a test
static void set_setting(const gchar *schema_name, const gchar *key_name,
                        const gchar *value) {
  g_autofree gchar *filename =
      g_build_filename(temp_dir, "glib-2.0", "settings", "keyfile", NULL);
  g_autoptr(GSettingsBackend) backend =
      g_keyfile_settings_backend_new(filename, "/", NULL);
  g_autoptr(GSettings) settings =
      g_settings_new_with_backend(schema_name, backend);
  g_settings_set_string(settings, key_name, value);
}

// TICS -DEADCODE: it's a test
static void send_snapd_response(SoupServerMessage *message, guint status_code,
                                const gchar *json) {
  soup_server_message_set_status(message, status_code, "");
  SoupMessageHeaders *headers =
      soup_server_message_get_response_headers(message);
  soup_message_headers_set_content_type(headers, "application/json", NULL);
  soup_message_headers_set_content_length(headers, strlen(json));
  soup_message_body_append(soup_server_message_get_response_body(message),
                           SOUP_MEMORY_COPY, json, strlen(json));
}

// TICS -DEADCODE: it's a test
static void handle_snapd_themes_request(SoupServerMessage *message) {
  const gchar *query = g_uri_get_query(soup_server_message_get_uri(message));
  switch (state) {
  case STATE_GET_EXISTING_THEME_STATUS:
    g_assert_cmpstr(soup_server_message_get_method(message), ==, "GET");
    g_assert_cmpstr(query, ==,
                    "gtk-theme=GtkTheme1&icon-theme=IconTheme1&icon-theme="
                    "CursorTheme1&sound-theme=SoundTheme1");
    send_snapd_response(
        message, 200,
        "{\"type\":\"sync\",\"status-code\":200,\"status\":\"OK\",\"result\":{"
        "\"gtk-themes\":{\"GtkTheme1\":\"installed\"},\"icon-themes\":{"
        "\"IconTheme1\":\"installed\",\"CursorTheme1\":\"installed\"},\"sound-"
        "themes\":{\"SoundTheme1\":\"installed\"}}}");

    // After first contact, change the themes.
    set_setting("org.gnome.desktop.interface", "gtk-theme", "GtkTheme2");
    set_setting("org.gnome.desktop.interface", "icon-theme", "IconTheme2");
    set_setting("org.gnome.desktop.interface", "cursor-theme", "CursorTheme2");
    set_setting("org.gnome.desktop.sound", "theme-name", "SoundTheme2");

    state = STATE_GET_NEW_THEME_STATUS;
    break;
  case STATE_GET_NEW_THEME_STATUS:
    g_assert_cmpstr(soup_server_message_get_method(message), ==, "GET");
    g_assert_cmpstr(query, ==,
                    "gtk-theme=GtkTheme2&icon-theme=IconTheme2&icon-theme="
                    "CursorTheme2&sound-theme=SoundTheme2");
    send_snapd_response(
        message, 200,
        "{\"type\":\"sync\",\"status-code\":200,\"status\":\"OK\",\"result\":{"
        "\"gtk-themes\":{\"GtkTheme2\":\"available\"},\"icon-themes\":{"
        "\"IconTheme2\":\"installed\",\"CursorTheme2\":\"unavailable\"},"
        "\"sound-themes\":{\"SoundTheme2\":\"available\"}}}");
    state = STATE_PROMPT_INSTALL;
    break;
  case STATE_INSTALL_THEMES:
    g_assert_cmpstr(soup_server_message_get_method(message), ==, "POST");
    g_assert_cmpstr(soup_message_headers_get_content_type(
                        soup_server_message_get_request_headers(message), NULL),
                    ==, "application/json");
    g_autofree gchar *json =
        get_json(soup_server_message_get_request_body(message));
    g_assert_cmpstr(json, ==,
                    "{\"gtk-themes\":[\"GtkTheme2\"],\"icon-themes\":[],"
                    "\"sound-themes\":[\"SoundTheme2\"]}");
    send_snapd_response(message, 200,
                        "{\"type\":\"async\", \"change\": \"1234\"}");
    state = STATE_NOTIFY_COMPLETE;
    break;
  default:
    break;
  }
}

// TICS -DEADCODE: it's a test
static void handle_snapd_get_changes_request(SoupServerMessage *message) {
  send_snapd_response(message, 200,
                      "{\"type\":\"sync\",\"status-code\":200,\"status\":"
                      "\"OK\",\"result\":{\"id\":\"1234\",\"ready\":true}}");
}

// TICS -DEADCODE: it's a test
static void handle_snapd_request(SoupServer *server, SoupServerMessage *message,
                                 const char *path, GHashTable *query,
                                 gpointer user_data) {
  if (strcmp(path, "/v2/accessories/themes") == 0) {
    handle_snapd_themes_request(message);
  } else if (strcmp(path, "/v2/accessories/changes/1234") == 0 &&
             strcmp(soup_server_message_get_method(message), "GET") == 0) {
    handle_snapd_get_changes_request(message);
  } else {
    send_snapd_response(
        message, 404,
        "{\"type\":\"error\",\"status-code\":404,\"status\":\"Not "
        "Found\",\"result\":{\"message\":\"not found\"}}");
  }
}

// TICS -DEADCODE: it's a test
static gboolean setup_mock_snapd(GError **error) {
  snapd_socket_path = g_build_filename(temp_dir, "snapd.socket", NULL);

  g_autoptr(GSocket) socket =
      g_socket_new(G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM,
                   G_SOCKET_PROTOCOL_DEFAULT, error);
  if (socket == NULL) {
    g_prefix_error(error, "Failed to make socket: ");
    return FALSE;
  }
  g_autoptr(GSocketAddress) address =
      g_unix_socket_address_new(snapd_socket_path);
  if (!g_socket_bind(socket, address, TRUE, error)) {
    g_prefix_error(error, "Failed to make socket: ");
    return FALSE;
  }
  if (!g_socket_listen(socket, error)) {
    g_prefix_error(error, "Failed to listen: ");
    return FALSE;
  }

  snapd_server = soup_server_new("server-header", "MockSnapd/1.0", NULL);
  soup_server_add_handler(snapd_server, NULL, handle_snapd_request, NULL, NULL);
  if (!soup_server_listen_socket(snapd_server, socket, 0, error)) {
    g_prefix_error(error, "Failed to listen for HTTP requests: ");
    return FALSE;
  }

  return TRUE;
}

// TICS -DEADCODE: it's a test
static gboolean setup_session_bus(GError **error) {
  int address_pipe_fds[2];
  if (!g_unix_open_pipe(address_pipe_fds, FD_CLOEXEC, error)) {
    g_prefix_error(error, "Failed to open pipe for D-Bus bus: ");
    return FALSE;
  }
  g_autoptr(GSubprocessLauncher) launcher = g_subprocess_launcher_new(
      G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);
  g_subprocess_launcher_take_fd(launcher, address_pipe_fds[1],
                                address_pipe_fds[1]);
  g_autofree gchar *address_fd_arg = g_strdup_printf("%d", address_pipe_fds[1]);
  dbus_subprocess = g_subprocess_launcher_spawn(
      launcher, error, "dbus-daemon", "--nofork", "--session",
      "--print-address", address_fd_arg, NULL);
  if (dbus_subprocess == NULL) {
    g_prefix_error(error, "Failed to launch dbus-daemon: ");
    return FALSE;
  }

  gchar address[1024];
  size_t n_read = read(address_pipe_fds[0], address, 1023);
  close(address_pipe_fds[0]);
  if (n_read < 0) {
    return FALSE;
  }
  address[n_read] = '\0';
  g_strstrip(address);
  dbus_address = g_strdup(address);

  return TRUE;
}

// TICS -DEADCODE: it's a test
static gboolean launch_snapd_desktop_integration() {
  g_autoptr(GSubprocessLauncher) launcher =
      g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_NONE);
  g_subprocess_launcher_setenv(launcher, "LC_ALL", "C", TRUE);
  g_subprocess_launcher_setenv(launcher, "LANG", "C", TRUE);
  g_subprocess_launcher_setenv(launcher, "XDG_CONFIG_HOME", temp_dir, TRUE);
  g_subprocess_launcher_setenv(launcher, "GSETTINGS_BACKEND", "keyfile", TRUE);
  g_subprocess_launcher_setenv(launcher, "DBUS_SESSION_BUS_ADDRESS",
                               dbus_address, TRUE);

  g_autofree gchar *daemon_path =
      g_build_filename(DAEMON_BUILDDIR, "snapd-desktop-integration", NULL);
  g_autofree gchar *snapd_socket_path_arg =
      g_strdup_printf("--snapd-socket-path=%s", snapd_socket_path);

  g_autoptr(GError) error = NULL;
  g_autoptr(GSubprocess) subprocess = g_subprocess_launcher_spawn(
      launcher, &error, daemon_path, snapd_socket_path_arg, NULL);
  if (subprocess == NULL) {
    return FALSE;
  }

  return TRUE;
}

// TICS -DEADCODE: it's a test
static void notifications_name_acquired_cb(GDBusConnection *connection,
                                           const gchar *name,
                                           gpointer user_data) {
  g_assert_true(launch_snapd_desktop_integration());
}

// TICS -DEADCODE: it's a test
static void handle_notifications_method_call(
    GDBusConnection *connection, const gchar *sender, const gchar *object_path,
    const gchar *interface_name, const gchar *method_name, GVariant *parameters,
    GDBusMethodInvocation *invocation, gpointer user_data) {
  if (strcmp(interface_name, "org.freedesktop.Notifications") != 0) {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_UNKNOWN_INTERFACE,
                                          "Unknown interface");
    return;
  }

  if (strcmp(method_name, "GetServerInformation") == 0) {
    g_dbus_method_invocation_return_value(
        invocation, g_variant_new("(ssss)", "mock-notifications-server", "Test",
                                  "1.0", "1.2"));
  } else if (strcmp(method_name, "GetCapabilities") == 0) {
    g_autoptr(GVariantBuilder) capabilities =
        g_variant_builder_new(G_VARIANT_TYPE("as"));
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(as)", capabilities));
  } else if (strcmp(method_name, "Notify") == 0) {
    const gchar *app_name, *app_icon, *summary, *body;
    guint32 replaces_id;
    g_auto(GStrv) actions;
    g_autoptr(GVariantIter) hints = NULL;
    gint32 expire_timeout;
    g_variant_get(parameters, "(&su&s&s&s^asa{sv}i)", &app_name, &replaces_id,
                  &app_icon, &summary, &body, &actions, &hints,
                  &expire_timeout);
    guint32 notification_id = next_notification_id;
    next_notification_id++;
    g_dbus_method_invocation_return_value(
        invocation, g_variant_new("(u)", notification_id));

    if (state == STATE_PROMPT_INSTALL) {
      g_assert_cmpstr(summary, ==, "Some required theme snaps are missing.");
      g_assert_cmpstr(body, ==, "Would you like to install them now?");
      g_assert_cmpint(g_strv_length(actions), ==, 6);
      g_assert_cmpstr(actions[0], ==, "yes");
      g_assert_cmpstr(actions[1], ==, "Yes");
      g_assert_cmpstr(actions[2], ==, "no");
      g_assert_cmpstr(actions[3], ==, "No");
      g_assert_cmpstr(actions[4], ==, "default");
      g_assert_cmpstr(actions[5], ==, "default");

      g_assert_true(g_dbus_connection_emit_signal(
          connection, NULL, "/org/freedesktop/Notifications",
          "org.freedesktop.Notifications", "ActionInvoked",
          g_variant_new("(us)", notification_id, "yes"), NULL));

      state = STATE_INSTALL_THEMES;
    } else if (state == STATE_NOTIFY_COMPLETE) {
      g_assert_cmpstr(summary, ==, "Installing missing theme snaps:");
      g_assert_cmpstr(body, ==, "Complete.");
      exit_code = EXIT_SUCCESS;
      g_main_loop_quit(loop);
    }
  } else {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "Unknown method");
  }
}

// TICS -DEADCODE: it's a test
static gboolean setup_mock_notifications(GError **error) {
  g_autoptr(GDBusConnection) connection =
      g_dbus_connection_new_for_address_sync(
          dbus_address,
          G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
              G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
          NULL, NULL, error);
  if (connection == NULL) {
    g_prefix_error(error, "Failed to connect to session bus: ");
    return FALSE;
  }

  g_bus_own_name_on_connection(
      connection, "org.freedesktop.Notifications", G_BUS_NAME_OWNER_FLAGS_NONE,
      notifications_name_acquired_cb, NULL, NULL, NULL);

  static const gchar introspection_xml[] =
      "<node>"
      "  <interface name='org.freedesktop.Notifications'>"
      "    <method name='Notify'>"
      "      <arg type='s' direction='in'/>"
      "      <arg type='u' direction='in'/>"
      "      <arg type='s' direction='in'/>"
      "      <arg type='s' direction='in'/>"
      "      <arg type='s' direction='in'/>"
      "      <arg type='as' direction='in'/>"
      "      <arg type='a{sv}' direction='in'/>"
      "      <arg type='i' direction='in'/>"
      "      <arg type='u' direction='out'/>"
      "    </method>"
      "    <method name='GetCapabilities'>"
      "      <arg type='as' direction='out'/>"
      "    </method>"
      "    <method name='GetServerInformation'>"
      "      <arg type='s' direction='out'/>"
      "      <arg type='s' direction='out'/>"
      "      <arg type='s' direction='out'/>"
      "      <arg type='s' direction='out'/>"
      "    </method>"
      "    <signal name='NotificationClosed'>"
      "      <arg type='u'/>"
      "      <arg type='u'/>"
      "    </signal>"
      "    <signal name='ActionInvoked'>"
      "      <arg type='u'/>"
      "      <arg type='s'/>"
      "    </signal>"
      "  </interface>"
      "</node>";
  g_autoptr(GDBusNodeInfo) node_info =
      g_dbus_node_info_new_for_xml(introspection_xml, error);
  if (node_info == NULL) {
    g_prefix_error(error, "Failed to parse D-Bus XML: ");
    return FALSE;
  }

  static const GDBusInterfaceVTable notifications_vtable = {
      handle_notifications_method_call,
      NULL,
      NULL,
  };
  if (g_dbus_connection_register_object(
          connection, "/org/freedesktop/Notifications",
          node_info->interfaces[0], &notifications_vtable, NULL, NULL,
          error) == 0) {
    g_prefix_error(error, "Failed to register notifications object: ");
    return FALSE;
  }

  return TRUE;
}

// TICS -DEADCODE: it's a test
int main(int argc, char **argv) {
  loop = g_main_loop_new(NULL, FALSE);

  g_autoptr(GError) error = NULL;
  temp_dir = g_dir_make_tmp("snapd-desktop-integration-XXXXXX", &error);
  if (temp_dir == NULL) {
    g_printerr("Failed to make temporary directory: %s\n", error->message);
    return EXIT_FAILURE;
  }

  set_setting("org.gnome.desktop.interface", "gtk-theme", "GtkTheme1");
  set_setting("org.gnome.desktop.interface", "icon-theme", "IconTheme1");
  set_setting("org.gnome.desktop.interface", "cursor-theme", "CursorTheme1");
  set_setting("org.gnome.desktop.sound", "theme-name", "SoundTheme1");

  if (!setup_mock_snapd(&error)) {
    g_printerr("Failed to setup mock snapd: %s\n", error->message);
    return EXIT_FAILURE;
  }

  if (!setup_session_bus(&error)) {
    g_printerr("Failed to setup session bus: %s\n", error->message);
    return EXIT_FAILURE;
  }

  if (!setup_mock_notifications(&error)) {
    g_printerr("Failed to setup mock notifications server: %s\n",
               error->message);
    return EXIT_FAILURE;
  }

  g_main_loop_run(loop);

  g_print("Tests passed\n");
  if (dbus_subprocess != NULL) {
    g_print("Killing subprocesses\n");
    g_subprocess_force_exit(dbus_subprocess);
  }

  g_clear_object(&snapd_server);
  g_clear_object(&dbus_subprocess);

  return exit_code;
}
