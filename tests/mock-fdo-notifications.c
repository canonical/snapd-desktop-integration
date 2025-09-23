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

#include "mock-fdo-notifications.h"

#include <gio/gio.h>
#include <glib-unix.h>
#include <poll.h>
#include <stdbool.h>
#include <unistd.h>

/*
 * Mock FDO Notifications
 *
 * This class emulates the Freedesktop notifications DBus interface,
 * allowing tests to receive the parameters of any new notification, and
 * to send user actions over them.
 *
 * To be able to work in CI environments, a stand-alone session DBus server
 * must be launched. For this, the `mock_fdo_notifications_setup_session_bus()`
 * function is provided, which must be run at the beginning of the main()
 * function because it modifies the environment to re-define the
 * DBUS_SESSION_BUS_ADDRESS variable, thus it must be run before initializing
 * any library.
 *
 * The class itself, MockFdoNotifications, must run part of its code in another
 * process. Not doing so results in libraries like libnotify not working
 * correctly (the first DBus message arrives about 20 seconds later, and no new
 * messages will arrive to the server). This is done automagically by the
 * `mock_fdo_notifications_run()` method.
 *
 * After creating an object and call the `mock_fdo_notifications_run()` method,
 * it is possible to wait for a notification just by calling the method
 * `mock_fdo_notifications_wait_for_notification()`. It will return a pointer
 * to a `MockNotificationsData` structure (which belongs to the object, so it
 * must NOT be freed) with the notification data, or NULL if the timeout
 * expired.
 *
 * Another method is `mock_fdo_notifications_send_action()`, which allows to
 * emulate the user clicking on an action of a notification.
 *
 * Finally, the object can emit the `notification-closed` signal when the
 * notification is removed from the system tray.
 * */

struct _MockFdoNotifications {
  GObject parent_instance;

  GDBusConnection *connection;
  GSubprocess *dbus_subprocess;
  gchar *dbus_address;
  GApplication *app;
  MockNotificationsData last_notification_data;
  gboolean updated;
  guint32 current_uid;
  int notification_pipes[2];
  int actions_pipes[2];
  guint actions_source;
};

G_DEFINE_TYPE(MockFdoNotifications, mock_fdo_notifications, G_TYPE_OBJECT)

static void handle_notifications_method_call_child(
    GDBusConnection *connection, const gchar *sender, const gchar *object_path,
    const gchar *interface_name, const gchar *method_name, GVariant *parameters,
    GDBusMethodInvocation *invocation, gpointer user_data) {
  MockFdoNotifications *self = MOCK_FDO_NOTIFICATIONS(user_data);

  g_print("Handle notifications %s, %s, %s, %s\n", sender, object_path,
          interface_name, method_name);
  if (strcmp(interface_name, "org.freedesktop.Notifications") != 0) {
    g_print("Asked for interface %s\n", interface_name);
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_UNKNOWN_INTERFACE,
                                          "Unknown interface");
    return;
  }

  if (strcmp(method_name, "GetServerInformation") == 0) {
    g_dbus_method_invocation_return_value(
        invocation,
        g_variant_new("(ssss)", "gnome-shell", "GNOME", "47.0", "1.2"));
  } else if (strcmp(method_name, "GetCapabilities") == 0) {
    g_autoptr(GVariantBuilder) capabilities =
        g_variant_builder_new(G_VARIANT_TYPE("as"));
    g_variant_builder_add(capabilities, "s", "actions");
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(as)", capabilities));
  } else if (strcmp(method_name, "Notify") == 0) {
    self->current_uid++;
    gsize parameters_size = g_variant_get_size(parameters);
    g_autofree gpointer serialized_parameters = g_malloc(parameters_size);
    g_variant_store(parameters, serialized_parameters);

    /* We must send the parameters over a pipe because this part is running in
     * one process and the tests themselves are running in a different one.
     */

    // First, we send the size of the GVariant
    write(self->notification_pipes[1], &parameters_size,
          sizeof(parameters_size));
    // Then the serialized GVariant
    write(self->notification_pipes[1], serialized_parameters, parameters_size);
    // And finally, the notification ID
    write(self->notification_pipes[1], &(self->current_uid),
          sizeof(self->current_uid));

    g_dbus_method_invocation_return_value(
        invocation, g_variant_new("(u)", self->current_uid));
  }
}

/**
 * mock_fdo_notifications_wait_for_notification
 * @mock: a #MockFdoNotifications
 * @timeout: timeout in milliseconds
 *
 * Waits for a notification to arrive to the server, and returns its data.
 *
 * Returns: (transfer none) (allow-none): a pointer to a #MockNotificationsData
 * struct with the data of the received notification, or %NULL if timed out. The
 * struct belongs to @mock, so it must not be freed.
 */

MockNotificationsData *
mock_fdo_notifications_wait_for_notification(MockFdoNotifications *self,
                                             guint timeout) {
  gsize parameters_size;
  g_autofree gpointer serialized_parameters = NULL;
  g_autoptr(GVariant) parameters = NULL;
  int read_size;
  struct pollfd poll_fd;
  poll_fd.fd = self->notification_pipes[0];
  poll_fd.events = POLLIN;
  poll_fd.revents = 0;

  if (poll(&poll_fd, 1, timeout) <= 0) {
    return NULL;
  }

  read_size = read(self->notification_pipes[0], &parameters_size,
                   sizeof(parameters_size));
  if (read_size != sizeof(parameters_size)) {
    return NULL;
  }
  serialized_parameters = g_malloc(parameters_size);
  read_size =
      read(self->notification_pipes[0], serialized_parameters, parameters_size);
  if (read_size != parameters_size) {
    return NULL;
  }

  parameters = g_variant_new_from_data(g_variant_type_new("(susssasa{sv}i)"),
                                       serialized_parameters, parameters_size,
                                       TRUE, g_free, serialized_parameters);
  serialized_parameters = NULL; // is freed by the GVariant constructor
  g_auto(GStrv) actions;
  g_autofree char *parameters_str = g_variant_print(parameters, true);
  g_test_message("Notify Called with parameters: %s", parameters_str);
  g_variant_get(parameters, "(&su&s&s&s^asa{sv}i)",
                &self->last_notification_data.app_name,
                &self->last_notification_data.replaces_id,
                &self->last_notification_data.icon_path,
                &self->last_notification_data.title,
                &self->last_notification_data.body, &actions,
                &self->last_notification_data.hints,
                &self->last_notification_data.expire_timeout);
  self->last_notification_data.actions = g_strdupv(actions);

  read_size =
      read(self->notification_pipes[0], &self->last_notification_data.uid,
           sizeof(self->last_notification_data.uid));
  if (read_size != sizeof(self->last_notification_data.uid)) {
    return NULL;
  }
  return &self->last_notification_data;
}

/**
 * mock_fdo_notifications_send_action
 * @mock: a #MockFdoNotifications
 * @uid: a notification UID, received through the MockNotificationsData struct
 * @action: the action to send to that notification
 *
 * Sends the specified action to the notification with the passed UID. It
 * allows to emulate the user clicking on a notification.
 */
void mock_fdo_notifications_send_action(MockFdoNotifications *self, guint32 uid,
                                        gchar *action) {
  size_t action_size = strlen(action) + 1;
  write(self->actions_pipes[1], &uid, sizeof(uid));
  write(self->actions_pipes[1], &action_size, sizeof(action_size));
  write(self->actions_pipes[1], action, action_size);
}

static gboolean send_action(guint fd, GIOCondition condition,
                            MockFdoNotifications *self) {
  guint32 uid;
  size_t action_size;
  g_autofree gchar *action = NULL;
  int read_size;

  read_size = read(fd, &uid, sizeof(uid));
  if (read_size != sizeof(uid)) {
    exit(-1);
  }
  read_size = read(fd, &action_size, sizeof(action_size));
  action = g_malloc(action_size);
  read_size = read(fd, action, action_size);
  if (read_size != action_size) {
    exit(-1);
  }

  g_autoptr(GVariant) data = g_variant_new("(us)", uid, action);
  g_dbus_connection_emit_signal(self->connection, NULL,
                                "/org/freedesktop/Notifications",
                                "org.freedesktop.Notifications",
                                "ActionInvoked", g_steal_pointer(&data), NULL);
  data = g_variant_new("(uu)", uid, 2); // dismissed by the user
  g_dbus_connection_emit_signal(
      self->connection, NULL, "/org/freedesktop/Notifications",
      "org.freedesktop.Notifications", "NotificationClosed",
      g_steal_pointer(&data), NULL);
  return TRUE;
}

static gboolean setup_mock_notifications_dbus_server(MockFdoNotifications *self,
                                                     GError **error) {
  self->connection = g_dbus_connection_new_for_address_sync(
      g_getenv("DBUS_SESSION_BUS_ADDRESS"),
      G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
          G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
      NULL, NULL, error);
  if (self->connection == NULL) {
    g_prefix_error(error, "Failed to connect to session bus: ");
    return FALSE;
  }

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

  static const GDBusInterfaceVTable vtable = {
      handle_notifications_method_call_child, NULL, NULL};

  g_autoptr(GDBusNodeInfo) node_info =
      g_dbus_node_info_new_for_xml(introspection_xml, error);
  if (node_info == NULL) {
    g_prefix_error(error, "Failed to parse D-Bus XML: ");
    return FALSE;
  }

  if (g_dbus_connection_register_object(
          self->connection, "/org/freedesktop/Notifications",
          node_info->interfaces[0], &vtable, g_object_ref(self), g_object_unref,
          error) == 0) {
    g_prefix_error(error, "Failed to register notifications object: ");
    return FALSE;
  }

  g_bus_own_name_on_connection(self->connection,
                               "org.freedesktop.Notifications",
                               G_BUS_NAME_OWNER_FLAGS_NONE, NULL, NULL,
                               g_object_ref(self), g_object_unref);
  return TRUE;
}

static void mock_fdo_notifications_dispose(GObject *object) {
  MockFdoNotifications *self = MOCK_FDO_NOTIFICATIONS(object);

  close(self->notification_pipes[0]);
  close(self->notification_pipes[1]);
  close(self->actions_pipes[0]);
  close(self->actions_pipes[1]);
  g_clear_pointer(&self->dbus_address, g_free);
  if (self->dbus_subprocess) {
    g_subprocess_force_exit(self->dbus_subprocess);
    g_clear_object(&self->dbus_subprocess);
  }

  G_OBJECT_CLASS(mock_fdo_notifications_parent_class)->dispose(object);
}

static void do_activate(GApplication *application, MockFdoNotifications *self) {
  // ensure that the g_application won't quit
  g_application_hold(application);
  // register the org.freedesktop.Notifications well-known DBus name
  GError *error = NULL;
  if (!setup_mock_notifications_dbus_server(self, &error)) {
    g_error("Failed to setup notifications: %s", error->message);
  }
  // notify that the initialization is complete
  gchar buffer = 'A'; // any value is fine
  write(self->notification_pipes[1], &buffer, 1);
}

/**
 * mock_fdo_notifications_run
 * @mock: a #MockFdoNotifications
 * @argc: the argc parameter from main()
 * @argv: the argv parameter from main()
 *
 * Initializes the notify mockup, and returns only when it is ready
 * to receive DBus messages.
 */
void mock_fdo_notifications_run(MockFdoNotifications *self, int argc,
                                char **argv) {
  // it is a must to run the server in another process
  int pid = fork();
  if (pid != 0) {
    gchar buffer;
    // wait until the child process has completed initialization
    read(self->notification_pipes[0], &buffer, 1);
    return;
  }

  self->app = g_application_new("io.snapcraft.MockNotifications",
                                G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(self->app, "activate", (GCallback)do_activate, self);
  self->actions_source = g_unix_fd_add(self->actions_pipes[0], G_IO_IN,
                                       (GUnixFDSourceFunc)send_action, self);
  g_application_run(self->app, argc, argv);
  exit(0);
}

static void mock_fdo_notifications_init(MockFdoNotifications *self) {
  pipe(self->notification_pipes);
  pipe(self->actions_pipes);
}

static void
mock_fdo_notifications_class_init(MockFdoNotificationsClass *klass) {
  G_OBJECT_CLASS(klass)->dispose = mock_fdo_notifications_dispose;
}

MockFdoNotifications *mock_fdo_notifications_new() {
  return g_object_new(MOCK_FDO_TYPE_NOTIFICATIONS, NULL);
}

/**
 * mock_fdo_notifications_setup_session_bus
 *
 * This function launches a new dbus-daemon for the session bus,
 * and sets DBUS_SESSION_BUS_ADDRESS to the right path. It must be
 * called at the beginning of main(), before anything else.
 */
gboolean mock_fdo_notifications_setup_session_bus(GError **error) {
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
  GSubprocess *dbus_subprocess = g_subprocess_launcher_spawn(
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
  setenv("DBUS_SESSION_BUS_ADDRESS", address, TRUE);
  g_print("DBUS_SESSION_BUS_ADDRESS=%s\n", address);

  return TRUE;
}
