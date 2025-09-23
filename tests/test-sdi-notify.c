#include "../src/sdi-notify.h"
#include "gtk/gtk.h"
#include "mock-fdo-notifications.h"
#include <sys/stat.h>

/**
 * Unitary tests
 *
 * Since these tests work with the notifications system, they can't be
 * easily integrated into CI because they require to be able to show
 * true notifications. Anyway, this code shouldn't need to be touched
 * too much, so having to run these tests manually should not be a
 * problem.
 *
 * To run these tests, just run this test program from a command line,
 * check that each notification has the text specified in the test
 * description (shown in the terminal),and click where the test description
 * tells you to click.
 *
 * It is strongly recommended to run this test with Valgrind with the
 * option --leak-check=full, to also detect memory leaks and wrong
 * memory accesses.
 */

#define SNAP_STORE_APP_NAME "Snap Store"

SdiNotify *notifier = NULL;

gchar *tmpdirpath = NULL;
gchar *snap_store_icon = NULL;
MockFdoNotifications *mock_notifications = NULL;

/**
 * Several help functions
 */

static gchar *get_data_path(gchar *resource_name) {
  g_autofree gchar *path =
      g_test_build_filename(G_TEST_BUILT, "data", resource_name, NULL);
  return g_canonicalize_filename(path, NULL);
}

static gchar *create_desktop_file(const char *name, const char *visible_name,
                                  const char *icon) {
  g_autofree gchar *filename = g_strdup_printf("%s.desktop", name);
  gchar *desktop_path =
      g_build_filename(tmpdirpath, "applications", filename, NULL);
  FILE *f = fopen(desktop_path, "w");
  g_assert_nonnull(f);
  fprintf(f,
          "[Desktop Entry]\nVersion=1.0\nType=Application\nExec=/usr/bin/true "
          "This is application %s\n",
          visible_name);
  if (visible_name != NULL) {
    fprintf(f, "Name=%s\n", visible_name);
  }
  if (icon != NULL) {
    fprintf(f, "Icon=%s\n", icon);
  }
  fclose(f);
  return desktop_path;
}

static SnapdApp *create_app(gchar *name, gchar *desktop_file) {
  return g_object_new(SNAPD_TYPE_APP, "name", name, "desktop-file",
                      desktop_file, NULL);
}

static GPtrArray *add_app(GPtrArray *array, gchar *name, gchar *desktop_file) {
  if (array == NULL) {
    array = g_ptr_array_new_full(1, g_object_unref);
  }
  g_ptr_array_add(array, (gpointer)create_app(name, desktop_file));
  return array;
}

static SnapdSnap *create_snap(gchar *snap_name, GPtrArray *apps) {
  return g_object_new(SNAPD_TYPE_SNAP, "apps", apps, "name", snap_name, NULL);
}

static gboolean has_action(GStrv actions, gchar *action, gchar *text) {
  gchar **p;
  for (p = actions; *p != NULL; p += 2) {
    if (g_str_equal(*p, action) &&
        ((text == NULL) || g_str_equal(*(p + 1), text))) {
      return TRUE;
    }
  }
  return FALSE;
}

static void assert_notification_hint(MockNotificationsData *data,
                                     const char *hint,
                                     GVariant *expected_value) {
  g_autoptr(GVariant) expected_ref = g_variant_ref_sink(expected_value);
  g_autoptr(GVariant) value = g_variant_lookup_value(
      data->hints, hint, g_variant_get_type(expected_value));
  g_assert_nonnull(value);

  g_autofree char *expected_value_str = g_variant_print(expected_value, TRUE);
  g_autofree char *value_str = g_variant_print(value, TRUE);
  g_test_message("Expected hint for %s: %s", hint, expected_value_str);
  g_test_message("Actual hint for %s: %s", hint, value_str);

  g_assert_true(g_variant_equal(value, expected_ref));
}

static gchar *wait_for_notification_close_cb(GObject *self, gchar *param,
                                             gpointer data) {
  static GMainLoop *loop = NULL;
  static gint sid = 0;
  static gchar *result = NULL;
  if (loop == NULL) {
    // first call; connect the signal and wait
    loop = g_main_loop_new(NULL, FALSE);
    sid = g_signal_connect(notifier, "notification-closed",
                           (GCallback)wait_for_notification_close_cb, NULL);
    g_main_loop_run(loop);
    g_signal_handler_disconnect(notifier, sid);
    g_main_loop_unref(loop);
    loop = NULL;
    return g_steal_pointer(&result);
  }
  result = g_strdup(param);
  g_main_loop_quit(loop);
  return NULL;
}

static gchar *wait_for_notification_close() {
  return wait_for_notification_close_cb(NULL, NULL, NULL);
}

/**
 * Test functions
 */

void test_update_available_1() {
  g_autofree gchar *icon_path = get_data_path("icon1.svg");
  g_autofree gchar *desktop_file =
      create_desktop_file("test1", "Test app 1", icon_path);

  g_autoptr(GPtrArray) apps = add_app(NULL, "test_app1", desktop_file);
  g_autoptr(SnapdSnap) snap = create_snap("test_snap1", apps);
  g_autoptr(GListStore) snaps = g_list_store_new(SNAPD_TYPE_SNAP);
  g_list_store_append(snaps, snap);
  sdi_notify_pending_refresh(notifier, G_LIST_MODEL(snaps));

  MockNotificationsData *data =
      mock_fdo_notifications_wait_for_notification(mock_notifications, 1000);
  g_assert_nonnull(data);

  g_assert_cmpstr(data->title, ==, "Update available for Test app 1");
  g_assert_cmpstr(data->body, ==, "Quit the app to update it now.");
  g_assert_cmpstr(data->icon_path, ==, snap_store_icon);
  g_assert_cmpstr(data->app_name, ==, SNAP_STORE_APP_NAME);
  assert_notification_hint(data, "image-path", g_variant_new_string(icon_path));
  g_assert_cmpint(g_strv_length(data->actions), ==, 6);
  g_assert_true(has_action(data->actions, "app.show-updates", "Show updates"));
  g_assert_true(has_action(data->actions, "default", NULL));
  g_assert_true(has_action(data->actions, "app.ignore-notification",
                           "Don't remind me again"));

  mock_fdo_notifications_send_action(mock_notifications, data->uid,
                                     "app.show-updates");

  g_autofree gchar *result = wait_for_notification_close(NULL, NULL);
  unlink(desktop_file); // delete desktop file
  g_assert_cmpstr(result, ==, "show-updates");
}

void test_update_available_2() {
  g_autofree gchar *icon_path = get_data_path("icon1.svg");
  g_autofree gchar *desktop_file =
      create_desktop_file("test2", "Test app 2", icon_path);

  g_autoptr(GPtrArray) apps = add_app(NULL, "test_app2", desktop_file);
  g_autoptr(SnapdSnap) snap = create_snap("test_snap2", apps);
  g_autoptr(GListStore) snaps = g_list_store_new(SNAPD_TYPE_SNAP);
  g_list_store_append(snaps, snap);
  sdi_notify_pending_refresh(notifier, G_LIST_MODEL(snaps));

  MockNotificationsData *data =
      mock_fdo_notifications_wait_for_notification(mock_notifications, 1000);
  g_assert_nonnull(data);

  g_assert_cmpstr(data->title, ==, "Update available for Test app 2");
  g_assert_cmpstr(data->body, ==, "Quit the app to update it now.");
  g_assert_cmpstr(data->icon_path, ==, snap_store_icon);
  g_assert_cmpstr(data->app_name, ==, SNAP_STORE_APP_NAME);
  assert_notification_hint(data, "image-path", g_variant_new_string(icon_path));
  g_assert_cmpint(g_strv_length(data->actions), ==, 6);
  g_assert_true(has_action(data->actions, "app.show-updates", "Show updates"));
  g_assert_true(has_action(data->actions, "default", NULL));
  g_assert_true(has_action(data->actions, "app.ignore-notification",
                           "Don't remind me again"));

  mock_fdo_notifications_send_action(mock_notifications, data->uid, "default");

  g_autofree gchar *result = wait_for_notification_close(NULL, NULL);
  unlink(desktop_file); // delete desktop file
  g_assert_cmpstr(result, ==, "show-updates");
}

static void test_ignore_event_cb(GObject *self, gchar *value, gpointer data) {
  static gint counter = 0;
  static gchar **string_list;
  if (self == NULL) {
    counter = 0;
    string_list = (gchar **)data;
    return;
  }
  g_assert_cmpstr(value, ==, string_list[counter]);
  counter++;
  gint *external_counter = (gint *)data;
  *external_counter = counter;
}

void test_update_available_3() {
  g_autofree gchar *icon_path = get_data_path("icon1.svg");
  g_autofree gchar *desktop_file =
      create_desktop_file("test3", "Test app 3", icon_path);

  g_autoptr(GPtrArray) apps = add_app(NULL, "test_app3", desktop_file);
  g_autoptr(SnapdSnap) snap = create_snap("test_snap3", apps);
  g_autoptr(GListStore) snaps = g_list_store_new(SNAPD_TYPE_SNAP);
  g_list_store_append(snaps, snap);
  sdi_notify_pending_refresh(notifier, G_LIST_MODEL(snaps));

  MockNotificationsData *data =
      mock_fdo_notifications_wait_for_notification(mock_notifications, 1000);
  g_assert_nonnull(data);

  g_assert_cmpstr(data->title, ==, "Update available for Test app 3");
  g_assert_cmpstr(data->body, ==, "Quit the app to update it now.");
  g_assert_cmpstr(data->icon_path, ==, snap_store_icon);
  g_assert_cmpstr(data->app_name, ==, SNAP_STORE_APP_NAME);
  assert_notification_hint(data, "image-path", g_variant_new_string(icon_path));
  g_assert_cmpint(g_strv_length(data->actions), ==, 6);
  g_assert_true(has_action(data->actions, "app.show-updates", "Show updates"));
  g_assert_true(has_action(data->actions, "default", NULL));
  g_assert_true(has_action(data->actions, "app.ignore-notification",
                           "Don't remind me again"));

  mock_fdo_notifications_send_action(mock_notifications, data->uid,
                                     "app.ignore-notification");

  gint signal_counter = 0;
  gchar *string_list[] = {"test_snap3"};
  test_ignore_event_cb(NULL, NULL, string_list); // initialize callback
  gint sid = g_signal_connect(notifier, "ignore-snap-event",
                              (GCallback)test_ignore_event_cb, &signal_counter);

  // run the test
  g_autofree gchar *result = wait_for_notification_close(NULL, NULL);
  unlink(desktop_file); // delete desktop file
  g_signal_handler_disconnect(notifier, sid);

  g_assert_cmpstr(result, ==, "ignore-snaps");
  g_assert_cmpint(signal_counter, ==, 1);
}

void test_update_available_4() {
  g_autofree gchar *icon_path = get_data_path("icon1.svg");
  g_autofree gchar *desktop_file1 =
      create_desktop_file("test4_1", "Test app 4_1", icon_path);
  g_autofree gchar *desktop_file2 =
      create_desktop_file("test4_2", "Test app 4_2", icon_path);

  g_autoptr(GPtrArray) apps1 = add_app(NULL, "test_app4_1", desktop_file1);
  g_autoptr(GPtrArray) apps2 = add_app(NULL, "test_app4_2", desktop_file2);
  g_autoptr(SnapdSnap) snap1 = create_snap("test_snap4_1", apps1);
  g_autoptr(SnapdSnap) snap2 = create_snap("test_snap4_2", apps2);
  g_autoptr(GListStore) snaps = g_list_store_new(SNAPD_TYPE_SNAP);
  g_list_store_append(snaps, snap1);
  g_list_store_append(snaps, snap2);
  sdi_notify_pending_refresh(notifier, G_LIST_MODEL(snaps));

  gint signal_counter = 0;
  gchar *string_list[] = {"test_snap4_1", "test_snap4_2"};
  test_ignore_event_cb(NULL, NULL, string_list); // initialize callback

  gint sid = g_signal_connect(notifier, "ignore-snap-event",
                              (GCallback)test_ignore_event_cb, &signal_counter);

  MockNotificationsData *data =
      mock_fdo_notifications_wait_for_notification(mock_notifications, 1000);
  g_assert_nonnull(data);

  g_assert_cmpstr(data->title, ==, "Updates available for 2 apps");
  g_assert_cmpstr(
      data->body, ==,
      "Test app 4_1 and Test app 4_2 will update when you quit them.");
  g_assert_cmpstr(data->icon_path, ==, snap_store_icon);
  g_assert_cmpstr(data->app_name, ==, SNAP_STORE_APP_NAME);
  assert_notification_hint(data, "image-path",
                           g_variant_new_string(snap_store_icon));
  g_assert_cmpint(g_strv_length(data->actions), ==, 6);
  g_assert_true(has_action(data->actions, "app.show-updates", "Show updates"));
  g_assert_true(has_action(data->actions, "default", NULL));
  g_assert_true(has_action(data->actions, "app.ignore-notification",
                           "Don't remind me again"));

  mock_fdo_notifications_send_action(mock_notifications, data->uid,
                                     "app.ignore-notification");

  g_autofree gchar *result = wait_for_notification_close(NULL, NULL);
  unlink(desktop_file1); // delete desktop file
  unlink(desktop_file2); // delete desktop file
  g_signal_handler_disconnect(notifier, sid);
  g_assert_cmpstr(result, ==, "ignore-snaps");
  g_assert_cmpint(signal_counter, ==, 2);
}

void test_update_available_5() {
  g_autofree gchar *icon_path = get_data_path("icon1.svg");
  g_autofree gchar *desktop_file1 =
      create_desktop_file("test5_1", "Test app 5_1", icon_path);
  g_autofree gchar *desktop_file2 =
      create_desktop_file("test5_2", "Test app 5_2", icon_path);
  g_autofree gchar *desktop_file3 =
      create_desktop_file("test5_3", "Test app 5_3", icon_path);

  g_autoptr(GPtrArray) apps1 = add_app(NULL, "test_app5_1", desktop_file1);
  g_autoptr(GPtrArray) apps2 = add_app(NULL, "test_app5_2", desktop_file2);
  g_autoptr(GPtrArray) apps3 = add_app(NULL, "test_app5_3", desktop_file3);
  g_autoptr(SnapdSnap) snap1 = create_snap("test_snap5_1", apps1);
  g_autoptr(SnapdSnap) snap2 = create_snap("test_snap5_2", apps2);
  g_autoptr(SnapdSnap) snap3 = create_snap("test_snap5_3", apps3);
  g_autoptr(GListStore) snaps = g_list_store_new(SNAPD_TYPE_SNAP);
  g_list_store_append(snaps, snap1);
  g_list_store_append(snaps, snap2);
  g_list_store_append(snaps, snap3);
  sdi_notify_pending_refresh(notifier, G_LIST_MODEL(snaps));

  gint signal_counter = 0;
  gchar *string_list[] = {"test_snap5_1", "test_snap5_2", "test_snap5_3"};
  test_ignore_event_cb(NULL, NULL, string_list); // initialize callback

  gint sid = g_signal_connect(notifier, "ignore-snap-event",
                              (GCallback)test_ignore_event_cb, &signal_counter);

  MockNotificationsData *data =
      mock_fdo_notifications_wait_for_notification(mock_notifications, 1000);
  g_assert_nonnull(data);

  g_assert_cmpstr(data->title, ==, "Updates available for 3 apps");
  g_assert_cmpstr(data->body, ==,
                  "Test app 5_1, Test app 5_2 and Test app 5_3 will update "
                  "when you quit them.");
  g_assert_cmpstr(data->icon_path, ==, snap_store_icon);
  g_assert_cmpstr(data->app_name, ==, SNAP_STORE_APP_NAME);
  assert_notification_hint(data, "image-path",
                           g_variant_new_string(snap_store_icon));
  g_assert_cmpint(g_strv_length(data->actions), ==, 6);
  g_assert_true(has_action(data->actions, "app.show-updates", "Show updates"));
  g_assert_true(has_action(data->actions, "default", NULL));
  g_assert_true(has_action(data->actions, "app.ignore-notification",
                           "Don't remind me again"));

  mock_fdo_notifications_send_action(mock_notifications, data->uid,
                                     "app.ignore-notification");

  g_autofree gchar *result = wait_for_notification_close(NULL, NULL);
  unlink(desktop_file1); // delete desktop file
  unlink(desktop_file2); // delete desktop file
  unlink(desktop_file3); // delete desktop file
  g_signal_handler_disconnect(notifier, sid);
  g_assert_cmpstr(result, ==, "ignore-snaps");
  g_assert_cmpint(signal_counter, ==, 3);
}

void test_update_available_6() {
  g_autofree gchar *icon_path = get_data_path("icon1.svg");
  g_autofree gchar *desktop_file1 =
      create_desktop_file("test6_1", "Test app 6_1", icon_path);
  g_autofree gchar *desktop_file2 =
      create_desktop_file("test6_2", "Test app 6_2", icon_path);
  g_autofree gchar *desktop_file3 =
      create_desktop_file("test6_3", "Test app 6_3", icon_path);
  g_autofree gchar *desktop_file4 =
      create_desktop_file("test6_4", "Test app 6_4", icon_path);

  g_autoptr(GPtrArray) apps1 = add_app(NULL, "test_app6_1", desktop_file1);
  g_autoptr(GPtrArray) apps2 = add_app(NULL, "test_app6_2", desktop_file2);
  g_autoptr(GPtrArray) apps3 = add_app(NULL, "test_app6_3", desktop_file3);
  g_autoptr(GPtrArray) apps4 = add_app(NULL, "test_app6_4", desktop_file4);
  g_autoptr(SnapdSnap) snap1 = create_snap("test_snap6_1", apps1);
  g_autoptr(SnapdSnap) snap2 = create_snap("test_snap6_2", apps2);
  g_autoptr(SnapdSnap) snap3 = create_snap("test_snap6_3", apps3);
  g_autoptr(SnapdSnap) snap4 = create_snap("test_snap6_4", apps4);
  g_autoptr(GListStore) snaps = g_list_store_new(SNAPD_TYPE_SNAP);
  g_list_store_append(snaps, snap1);
  g_list_store_append(snaps, snap2);
  g_list_store_append(snaps, snap3);
  g_list_store_append(snaps, snap4);
  sdi_notify_pending_refresh(notifier, G_LIST_MODEL(snaps));

  gint signal_counter = 0;
  gchar *string_list[] = {"test_snap6_1", "test_snap6_2", "test_snap6_3",
                          "test_snap6_4"};
  test_ignore_event_cb(NULL, NULL, string_list); // initialize callback

  gint sid = g_signal_connect(notifier, "ignore-snap-event",
                              (GCallback)test_ignore_event_cb, &signal_counter);

  MockNotificationsData *data =
      mock_fdo_notifications_wait_for_notification(mock_notifications, 1000);
  g_assert_nonnull(data);

  g_assert_cmpstr(data->title, ==, "Updates available for 4 apps");
  g_assert_cmpstr(data->body, ==, "Quit the apps to update them now.");
  g_assert_cmpstr(data->icon_path, ==, snap_store_icon);
  g_assert_cmpstr(data->app_name, ==, SNAP_STORE_APP_NAME);
  assert_notification_hint(data, "image-path",
                           g_variant_new_string(snap_store_icon));
  g_assert_cmpint(g_strv_length(data->actions), ==, 6);
  g_assert_true(has_action(data->actions, "app.show-updates", "Show updates"));
  g_assert_true(has_action(data->actions, "default", NULL));
  g_assert_true(has_action(data->actions, "app.ignore-notification",
                           "Don't remind me again"));

  mock_fdo_notifications_send_action(mock_notifications, data->uid,
                                     "app.ignore-notification");

  g_autofree gchar *result = wait_for_notification_close(NULL, NULL);
  unlink(desktop_file1); // delete desktop file
  unlink(desktop_file2); // delete desktop file
  unlink(desktop_file3); // delete desktop file
  unlink(desktop_file4); // delete desktop file
  g_signal_handler_disconnect(notifier, sid);
  g_assert_cmpstr(result, ==, "ignore-snaps");
  g_assert_cmpint(signal_counter, ==, 4);
}

void test_update_available_7() {
  g_autofree gchar *icon_path = get_data_path("icon1.svg");
  g_autofree gchar *desktop_file1 =
      create_desktop_file("test7", "Test app 7", icon_path);
  g_autoptr(GPtrArray) apps1 = add_app(NULL, "test_app7", desktop_file1);
  g_autoptr(SnapdSnap) snap1 = create_snap("test_snap7", apps1);
  sdi_notify_refresh_complete(notifier, snap1, "test_snap7");

  MockNotificationsData *data =
      mock_fdo_notifications_wait_for_notification(mock_notifications, 1000);
  g_assert_nonnull(data);

  g_assert_cmpstr(data->title, ==, "Test app 7 was updated");
  g_assert_cmpstr(data->body, ==, "You can reopen it now.");
  g_assert_cmpstr(data->icon_path, ==, snap_store_icon);
  g_assert_cmpstr(data->app_name, ==, SNAP_STORE_APP_NAME);
  assert_notification_hint(data, "image-path", g_variant_new_string(icon_path));
  g_assert_cmpint(g_strv_length(data->actions), ==, 2);
  g_assert_true(has_action(data->actions, "default", NULL));

  mock_fdo_notifications_send_action(mock_notifications, data->uid, "default");

  g_autofree gchar *result = wait_for_notification_close(NULL, NULL);
  unlink(desktop_file1); // delete desktop file
  g_autofree gchar *expected =
      g_strdup_printf("app-launch-updated %s", desktop_file1);
  g_assert_cmpstr(result, ==, expected);
}

void test_update_available_8() {
  g_autofree gchar *icon_path = get_data_path("icon1.svg");
  g_autofree gchar *desktop_file1 =
      create_desktop_file("test8", "Test app 8", icon_path);
  g_autoptr(GPtrArray) apps1 = add_app(NULL, "test_app8", desktop_file1);
  g_autoptr(SnapdSnap) snap1 = create_snap("test_snap8", apps1);
  sdi_notify_pending_refresh_forced(notifier, snap1, SECONDS_IN_A_DAY * 2,
                                    TRUE);
  MockNotificationsData *data =
      mock_fdo_notifications_wait_for_notification(mock_notifications, 1000);
  g_assert_nonnull(data);

  g_assert_cmpstr(data->title, ==, "Test app 8 will quit and update in 2 days");
  g_assert_cmpstr(data->body, ==,
                  "Save your progress and quit now to prevent data loss.");
  g_assert_cmpstr(data->icon_path, ==, snap_store_icon);
  g_assert_cmpstr(data->app_name, ==, SNAP_STORE_APP_NAME);
  assert_notification_hint(data, "image-path", g_variant_new_string(icon_path));
  g_assert_cmpint(g_strv_length(data->actions), ==, 6);
  g_assert_true(has_action(data->actions, "default", NULL));
  g_assert_true(has_action(data->actions, "app.show-updates", "Show updates"));
  g_assert_true(has_action(data->actions, "app.ignore-notification",
                           "Don't remind me again"));

  mock_fdo_notifications_send_action(mock_notifications, data->uid, "default");

  g_autofree gchar *result = wait_for_notification_close(NULL, NULL);
  unlink(desktop_file1); // delete desktop file
  g_assert_cmpstr(result, ==, "show-updates");
}

void test_update_available_9() {
  g_autofree gchar *icon_path = get_data_path("icon1.svg");
  g_autofree gchar *desktop_file1 =
      create_desktop_file("test9", "Test app 9", icon_path);
  g_autoptr(GPtrArray) apps1 = add_app(NULL, "test_app9", desktop_file1);
  g_autoptr(SnapdSnap) snap1 = create_snap("test_snap9", apps1);
  sdi_notify_pending_refresh_forced(notifier, snap1, SECONDS_IN_AN_HOUR * 5,
                                    FALSE);

  MockNotificationsData *data =
      mock_fdo_notifications_wait_for_notification(mock_notifications, 1000);
  g_assert_nonnull(data);

  g_assert_cmpstr(data->title, ==,
                  "Test app 9 will quit and update in 5 hours");
  g_assert_cmpstr(data->body, ==,
                  "Save your progress and quit now to prevent data loss.");
  g_assert_cmpstr(data->icon_path, ==, snap_store_icon);
  g_assert_cmpstr(data->app_name, ==, SNAP_STORE_APP_NAME);
  assert_notification_hint(data, "image-path", g_variant_new_string(icon_path));
  g_assert_cmpint(g_strv_length(data->actions), ==, 4);
  g_assert_true(has_action(data->actions, "default", NULL));
  g_assert_true(has_action(data->actions, "app.show-updates", "Show updates"));

  mock_fdo_notifications_send_action(mock_notifications, data->uid, "default");
  g_autofree gchar *result = wait_for_notification_close(NULL, NULL);
  unlink(desktop_file1); // delete desktop file
  g_assert_cmpstr(result, ==, "show-updates");
}

void test_update_available_10() {
  g_autofree gchar *icon_path = get_data_path("icon1.svg");
  g_autofree gchar *desktop_file1 =
      create_desktop_file("test10", "Test app 10", icon_path);
  g_autoptr(GPtrArray) apps1 = add_app(NULL, "test_app10", desktop_file1);
  g_autoptr(SnapdSnap) snap1 = create_snap("test_snap10", apps1);
  sdi_notify_pending_refresh_forced(notifier, snap1, SECONDS_IN_A_MINUTE * 13,
                                    TRUE);
  g_autofree gchar *store_updates_desktop_file = create_desktop_file(
      "snap-store_show-updates", "Snap Store Updates", NULL);

  gint signal_counter = 0;
  gchar *string_list[] = {"test_snap10"};
  test_ignore_event_cb(NULL, NULL, string_list); // initialize callback

  gint sid = g_signal_connect(notifier, "ignore-snap-event",
                              (GCallback)test_ignore_event_cb, &signal_counter);

  MockNotificationsData *data =
      mock_fdo_notifications_wait_for_notification(mock_notifications, 1000);
  g_assert_nonnull(data);

  g_assert_cmpstr(data->title, ==,
                  "Test app 10 will quit and update in 13 minutes");
  g_assert_cmpstr(data->body, ==,
                  "Save your progress and quit now to prevent data loss.");
  g_assert_cmpstr(data->icon_path, ==, snap_store_icon);
  g_assert_cmpstr(data->app_name, ==, SNAP_STORE_APP_NAME);
  assert_notification_hint(data, "image-path", g_variant_new_string(icon_path));
  g_assert_cmpint(g_strv_length(data->actions), ==, 6);
  g_assert_true(has_action(data->actions, "default", NULL));
  g_assert_true(has_action(data->actions, "app.show-updates", "Show updates"));
  g_assert_true(has_action(data->actions, "app.ignore-notification",
                           "Don't remind me again"));

  mock_fdo_notifications_send_action(mock_notifications, data->uid,
                                     "app.ignore-notification");

  g_autofree gchar *result = wait_for_notification_close(NULL, NULL);
  unlink(desktop_file1); // delete desktop file
  unlink(store_updates_desktop_file);
  g_signal_handler_disconnect(notifier, sid);
  g_assert_cmpstr(result, ==, "ignore-snaps");
  g_assert_cmpint(signal_counter, ==, 1);
}

/**
 * Notify emulator callbacks
 */

void run_tests() { g_print("Listo\n"); }

/**
 * GApplication callbacks
 */

static void do_startup(GApplication *app, gpointer data) {
  notifier = sdi_notify_new(app);
}

static void do_activate(GObject *object, gpointer data) {
  // because, by default, there are no windows, so the application would quit
  g_application_hold(G_APPLICATION(object));

  // add tests
  g_test_add_func("/update_available/test1", test_update_available_1);
  g_test_add_func("/update_available/test2", test_update_available_2);
  g_test_add_func("/update_available/test3", test_update_available_3);
  g_test_add_func("/update_available/test4", test_update_available_4);
  g_test_add_func("/update_available/test5", test_update_available_5);
  g_test_add_func("/update_available/test6", test_update_available_6);
  g_test_add_func("/update_done/test7", test_update_available_7);
  g_test_add_func("/update_forced/test8", test_update_available_8);
  g_test_add_func("/update_forced/test9", test_update_available_9);
  g_test_add_func("/update_forced/test10", test_update_available_10);

  g_test_run();
  g_application_release(G_APPLICATION(object));
}

int main(int argc, char **argv) {
  GError *error = NULL;
  setenv("LANG", "en_US", TRUE); // to ensure that string comparison is correct
  if (!mock_fdo_notifications_setup_session_bus(&error)) {
    g_error("Failed to set up a new dbus-daemon for the emulation: %s",
            error->message);
  }

  mock_notifications = mock_fdo_notifications_new();
  mock_fdo_notifications_run(mock_notifications, argc, argv);

  g_test_init(&argc, &argv, NULL);
  // here we will create any temporary files
  tmpdirpath = g_dir_make_tmp("test-sdi-notify-XXXXXXX", NULL);

  // This is needed to ensure that the gtk libraries can find the .desktop files
  g_autofree gchar *applications_path =
      g_build_filename(tmpdirpath, "applications", NULL);
  gchar *xdg_data_dirs = getenv("XDG_DATA_DIRS");
  g_autofree gchar *new_xdg_data_dirs =
      g_strdup_printf("%s%s%s", tmpdirpath, xdg_data_dirs ? ":" : "",
                      xdg_data_dirs ? xdg_data_dirs : "");
  mkdir(applications_path, 0777);
  setenv("XDG_DATA_DIRS", new_xdg_data_dirs, TRUE);
  g_test_message("Using XDG_DATA_DIRS: %s", new_xdg_data_dirs);

  // and since we cannot guarantee that the snap-store is installed, we fake it
  snap_store_icon = get_data_path("app-center.png");
  g_autofree gchar *store_desktop_file = create_desktop_file(
      "snap-store_snap-store", SNAP_STORE_APP_NAME, snap_store_icon);

  g_autoptr(GApplication) app = g_application_new("io.snapcraft.SdiNotifyTest",
                                                  G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "startup", (GCallback)do_startup, NULL);
  g_signal_connect(app, "activate", (GCallback)do_activate, NULL);
  g_application_run(app, argc, argv);
  g_free(tmpdirpath);
  g_free(snap_store_icon);
  return 0;
}
