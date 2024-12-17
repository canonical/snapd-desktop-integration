#include "../src/sdi-progress-dock.h"
#include "gtk/gtk.h"
SdiProgressDock *progress_dock = NULL;

static gboolean updating_value = FALSE;
static gboolean progress_visible_value = FALSE;
static gdouble progress_value = -1;
static gchar *expected_program = NULL;

enum CHANGES_LIST {
  CHANGES_PROGRESS = 1,
  CHANGES_PROGRESS_VISIBLE = 2,
  CHANGES_UPDATING = 4
} current_changes = 0;

static void set_progress_bar(gchar *desktop_file, guint done_tasks,
                             guint total_tasks, gboolean all_done) {
  current_changes = 0;
  g_autoptr(GStrvBuilder) desktop_files_builder = g_strv_builder_new();
  g_strv_builder_add(desktop_files_builder, desktop_file);
  g_auto(GStrv) desktop_files = g_strv_builder_end(desktop_files_builder);
  sdi_progress_dock_update_progress(progress_dock, "snap name", desktop_files,
                                    "a description", done_tasks, total_tasks,
                                    all_done);
}

static void timeoutCB(guint *timeout_id) {
  g_assert_cmpint(*timeout_id, !=, 0);
  *timeout_id = 0;
}

static void wait_for_events(guint timeout, guint changes) {
  GMainContext *context = g_main_context_default();
  guint timeout_id =
      g_timeout_add_once(timeout, (GSourceOnceFunc)timeoutCB, NULL);
  do {
    g_main_context_iteration(context, TRUE);
  } while ((timeout_id != 0) && (current_changes & changes) != changes);
  g_clear_handle_id(&timeout_id, g_source_remove);
}

// these are the actual tests

static void test_progress_bar() {
  g_assert_false(updating_value);
  g_assert_false(progress_visible_value);
  g_assert_cmpfloat_with_epsilon(progress_value, -1.0, DBL_EPSILON);

  expected_program = "program1.desktop";

  set_progress_bar("program1.desktop", 0, 10, FALSE);
  wait_for_events(4000, CHANGES_PROGRESS | CHANGES_PROGRESS_VISIBLE |
                            CHANGES_UPDATING);
  g_assert_true(updating_value);
  g_assert_true(progress_visible_value);
  g_assert_cmpfloat_with_epsilon(progress_value, 0 / 10.0, DBL_EPSILON);

  set_progress_bar("program1.desktop", 3, 10, FALSE);
  wait_for_events(4000, CHANGES_PROGRESS);
  g_assert_true(updating_value);
  g_assert_true(progress_visible_value);
  g_assert_cmpfloat_with_epsilon(progress_value, 3 / 10.0, DBL_EPSILON);

  set_progress_bar("program1.desktop", 10, 10, FALSE);
  wait_for_events(4000, CHANGES_PROGRESS);
  g_assert_true(updating_value);
  g_assert_true(progress_visible_value);
  g_assert_cmpfloat_with_epsilon(progress_value, 10 / 10.0, DBL_EPSILON);
}

static void test_updating() {
  g_assert_true(updating_value);
  g_assert_true(progress_visible_value);

  expected_program = "program1.desktop";

  set_progress_bar("program1.desktop", 0, 10, TRUE);
  wait_for_events(4000, CHANGES_PROGRESS_VISIBLE | CHANGES_UPDATING);
  g_assert_false(updating_value);
  g_assert_false(progress_visible_value);
}

/**
 * GApplication callbacks
 */

static void dockCB(GDBusConnection *connection, const gchar *sender_name,
                   const gchar *object_path, const gchar *interface_name,
                   const gchar *signal_name, GVariant *parameters,
                   gpointer user_data) {
  g_assert_cmpstr(g_variant_get_type_string(parameters), ==, "(sa{sv})");

  g_autoptr(GVariant) app_name_v = g_variant_get_child_value(parameters, 0);
  g_assert_nonnull(app_name_v);
  const gchar *app_name = g_variant_get_string(app_name_v, NULL);
  g_assert_nonnull(app_name);

  if (g_strcmp0(app_name, expected_program) != 0) {
    return;
  }

  g_autoptr(GVariant) properties = g_variant_get_child_value(parameters, 1);
  g_assert_nonnull(properties);
  for (int i = 0; i < g_variant_n_children(properties); i++) {
    g_autoptr(GVariant) element = g_variant_get_child_value(properties, i);
    g_assert_nonnull(element);
    g_autoptr(GVariant) key_v = g_variant_get_child_value(element, 0);
    g_assert_nonnull(key_v);
    g_autoptr(GVariant) value_container_v =
        g_variant_get_child_value(element, 1);
    g_assert_nonnull(value_container_v);
    g_autoptr(GVariant) value_v =
        g_variant_get_child_value(value_container_v, 0);
    g_assert_nonnull(value_v);
    const gchar *key = g_variant_get_string(key_v, NULL);
    if (!g_strcmp0(key, "progress")) {
      progress_value = g_variant_get_double(value_v);
      current_changes |= CHANGES_PROGRESS;
    } else if (g_strcmp0(key, "progress-visible")) {
      progress_visible_value = g_variant_get_boolean(value_v);
      current_changes |= CHANGES_PROGRESS_VISIBLE;
    } else if (g_strcmp0(key, "updating")) {
      updating_value = g_variant_get_boolean(value_v);
      current_changes |= CHANGES_UPDATING;
    }
  }
}

static void do_startup(GApplication *application, gpointer data) {
  progress_dock = sdi_progress_dock_new(application);
  g_autoptr(GDBusConnection) dbus_conn =
      g_application_get_dbus_connection(application);
  g_dbus_connection_signal_subscribe(
      dbus_conn, NULL, "com.canonical.Unity.LauncherEntry", "Update", NULL,
      NULL, G_DBUS_SIGNAL_FLAGS_NONE, (GDBusSignalCallback)dockCB, NULL, NULL);
}

static void do_activate(GApplication *app, gpointer data) {
  g_test_add_func("/dock/progress-bar", test_progress_bar);
  g_test_add_func("/dock/updating", test_updating);
  g_test_run();
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);

  g_autoptr(GApplication) app = g_application_new(
      "io.snapcraft.SdiProgressDockTest", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "startup", (GCallback)do_startup, NULL);
  g_signal_connect(app, "activate", (GCallback)do_activate, NULL);
  g_application_run(app, argc, argv);
}
