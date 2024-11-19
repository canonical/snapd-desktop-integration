#include "../src/sdi-forced-refresh-time-constants.h"
#include "../src/sdi-notify.h"
#include "gtk/gtk.h"

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

SdiNotify *notifier = NULL;

typedef struct _testData {
  const int test_number;
  const char *title;
  const char *description;
  const char *actions;
  const char *expected_results;
  const void (*test_function)(struct _testData *);
} TestData;

gchar *tmpdirpath = NULL;

/**
 * Several help functions
 */

static void describe_test(TestData *test) {
  g_assert_nonnull(test);
  g_assert_cmpint(test->test_number, !=, -1);
  // g_print("\e[1;1H\e[2J"); // clear screen
  g_print("\n\n\n\n");
  g_print("Test number %d\n", test->test_number);
  g_print("  Title: %s\n", test->title);
  g_print("  Description: %s\n\n", test->description);
  g_print("  Actions: %s\n", test->actions);
  if (test->expected_results != NULL)
    g_print("  Expected results: %s\n", test->expected_results);
  g_print("Waiting for actions\n");
}

static gchar *get_data_path(gchar *resource_name) {
  g_autofree gchar *path =
      g_test_build_filename(G_TEST_BUILT, "data", resource_name, NULL);
  return g_canonicalize_filename(path, NULL);
}

static gchar *create_desktop_file(gchar *name, gchar *visible_name,
                                  gchar *icon) {
  g_autofree gchar *filename = g_strdup_printf("%s.desktop", name);
  gchar *desktop_path = g_build_path("/", tmpdirpath, filename, NULL);
  FILE *f = fopen(desktop_path, "w");
  g_assert_nonnull(f);
  fprintf(
      f,
      "[Desktop Entry]\nVersion=1.0\nType=Application\nExec=/usr/bin/xmessage "
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

void test_update_available_1(TestData *test) {
  describe_test(test);

  g_autofree gchar *icon_path = get_data_path("icon1.svg");
  g_autofree gchar *desktop_file =
      create_desktop_file("test1", "Test app 1", icon_path);

  g_autoptr(GPtrArray) apps = add_app(NULL, "test_app1", desktop_file);
  g_autoptr(SnapdSnap) snap = create_snap("test_snap1", apps);
  g_autoptr(GListStore) snaps = g_list_store_new(SNAPD_TYPE_SNAP);
  g_list_store_append(snaps, snap);
  sdi_notify_pending_refresh(notifier, G_LIST_MODEL(snaps));

  g_autofree gchar *result = wait_for_notification_close(NULL, NULL);
  unlink(desktop_file); // delete desktop file
  g_assert_cmpstr(result, ==, "show-updates");
}

void test_update_available_2(TestData *test) {
  describe_test(test);

  g_autofree gchar *icon_path = get_data_path("icon1.svg");
  g_autofree gchar *desktop_file =
      create_desktop_file("test2", "Test app 2", icon_path);

  g_autoptr(GPtrArray) apps = add_app(NULL, "test_app2", desktop_file);
  g_autoptr(SnapdSnap) snap = create_snap("test_snap2", apps);
  g_autoptr(GListStore) snaps = g_list_store_new(SNAPD_TYPE_SNAP);
  g_list_store_append(snaps, snap);
  sdi_notify_pending_refresh(notifier, G_LIST_MODEL(snaps));

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

void test_update_available_3(TestData *test) {
  describe_test(test);

  g_autofree gchar *icon_path = get_data_path("icon1.svg");
  g_autofree gchar *desktop_file =
      create_desktop_file("test3", "Test app 3", icon_path);

  g_autoptr(GPtrArray) apps = add_app(NULL, "test_app3", desktop_file);
  g_autoptr(SnapdSnap) snap = create_snap("test_snap3", apps);
  g_autoptr(GListStore) snaps = g_list_store_new(SNAPD_TYPE_SNAP);
  g_list_store_append(snaps, snap);
  sdi_notify_pending_refresh(notifier, G_LIST_MODEL(snaps));

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

void test_update_available_4(TestData *test) {
  describe_test(test);

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

  g_autofree gchar *result = wait_for_notification_close(NULL, NULL);
  unlink(desktop_file1); // delete desktop file
  unlink(desktop_file2); // delete desktop file
  g_signal_handler_disconnect(notifier, sid);
  g_assert_cmpstr(result, ==, "ignore-snaps");
  g_assert_cmpint(signal_counter, ==, 2);
}

void test_update_available_5(TestData *test) {
  describe_test(test);

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

  g_autofree gchar *result = wait_for_notification_close(NULL, NULL);
  unlink(desktop_file1); // delete desktop file
  unlink(desktop_file2); // delete desktop file
  unlink(desktop_file3); // delete desktop file
  g_signal_handler_disconnect(notifier, sid);
  g_assert_cmpstr(result, ==, "ignore-snaps");
  g_assert_cmpint(signal_counter, ==, 3);
}

void test_update_available_6(TestData *test) {
  describe_test(test);

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

  g_autofree gchar *result = wait_for_notification_close(NULL, NULL);
  unlink(desktop_file1); // delete desktop file
  unlink(desktop_file2); // delete desktop file
  unlink(desktop_file3); // delete desktop file
  unlink(desktop_file4); // delete desktop file
  g_signal_handler_disconnect(notifier, sid);
  g_assert_cmpstr(result, ==, "ignore-snaps");
  g_assert_cmpint(signal_counter, ==, 4);
}

void test_update_available_7(TestData *test) {
  describe_test(test);

  g_autofree gchar *icon_path = get_data_path("icon1.svg");
  g_autofree gchar *desktop_file1 =
      create_desktop_file("test7", "Test app 7", icon_path);
  g_autoptr(GPtrArray) apps1 = add_app(NULL, "test_app7", desktop_file1);
  g_autoptr(SnapdSnap) snap1 = create_snap("test_snap7", apps1);
  sdi_notify_refresh_complete(notifier, snap1, "test_snap7");

  g_autofree gchar *result = wait_for_notification_close(NULL, NULL);
  unlink(desktop_file1); // delete desktop file
  g_autofree gchar *expected =
      g_strdup_printf("app-launch-updated %s", desktop_file1);
  g_assert_cmpstr(result, ==, expected);
}

void test_update_available_8(TestData *test) {
  describe_test(test);

  g_autofree gchar *icon_path = get_data_path("icon1.svg");
  g_autofree gchar *desktop_file1 =
      create_desktop_file("test8", "Test app 8", icon_path);
  g_autoptr(GPtrArray) apps1 = add_app(NULL, "test_app8", desktop_file1);
  g_autoptr(SnapdSnap) snap1 = create_snap("test_snap8", apps1);
  sdi_notify_pending_refresh_forced(notifier, snap1, SECONDS_IN_A_DAY * 2,
                                    TRUE);

  g_autofree gchar *result = wait_for_notification_close(NULL, NULL);
  unlink(desktop_file1); // delete desktop file
  g_assert_cmpstr(result, ==, "show-updates");
}

void test_update_available_9(TestData *test) {
  describe_test(test);

  g_autofree gchar *icon_path = get_data_path("icon1.svg");
  g_autofree gchar *desktop_file1 =
      create_desktop_file("test9", "Test app 9", icon_path);
  g_autoptr(GPtrArray) apps1 = add_app(NULL, "test_app9", desktop_file1);
  g_autoptr(SnapdSnap) snap1 = create_snap("test_snap9", apps1);
  sdi_notify_pending_refresh_forced(notifier, snap1, SECONDS_IN_AN_HOUR * 5,
                                    FALSE);

  g_autofree gchar *result = wait_for_notification_close(NULL, NULL);
  unlink(desktop_file1); // delete desktop file
  g_assert_cmpstr(result, ==, "show-updates");
}

void test_update_available_10(TestData *test) {
  describe_test(test);

  g_autofree gchar *icon_path = get_data_path("icon1.svg");
  g_autofree gchar *desktop_file1 =
      create_desktop_file("test10", "Test app 10", icon_path);
  g_autoptr(GPtrArray) apps1 = add_app(NULL, "test_app10", desktop_file1);
  g_autoptr(SnapdSnap) snap1 = create_snap("test_snap10", apps1);
  sdi_notify_pending_refresh_forced(notifier, snap1, SECONDS_IN_A_MINUTE * 13,
                                    TRUE);

  gint signal_counter = 0;
  gchar *string_list[] = {"test_snap10"};
  test_ignore_event_cb(NULL, NULL, string_list); // initialize callback

  gint sid = g_signal_connect(notifier, "ignore-snap-event",
                              (GCallback)test_ignore_event_cb, &signal_counter);

  g_autofree gchar *result = wait_for_notification_close(NULL, NULL);
  unlink(desktop_file1); // delete desktop file
  g_signal_handler_disconnect(notifier, sid);
  g_assert_cmpstr(result, ==, "ignore-snaps");
  g_assert_cmpint(signal_counter, ==, 1);
}

/**
 * Each test must have an entry in test_data
 */

TestData test_data[] = {
    {1, "/update_available/test1",
     "A notification with an icon that consists on a pale pink circle with a T "
     "inside, and the text 'Update available for Test app 1', 'Quit the app to "
     "update now' will appear, with two buttons: 'Show updates' and 'Don't "
     "remind me again'.",
     "Press the button 'Show updates'", NULL, test_update_available_1},
    {2, "/update_available/test2",
     "A notification with an icon that consists on a pale pink circle with a T "
     "inside, and the text 'Update available for Test app 2', 'Quit the app to "
     "update now' will appear, with two buttons: 'Show updates' and 'Don't "
     "remind me again'.",
     "Press the notification itself (but not the 'close' button) to launch the "
     "default action.",
     NULL, test_update_available_2},
    {3, "/update_available/test3",
     "A notification with an icon that consists on a pale pink circle with a T "
     "inside, and the text 'Update available for Test app 3', 'Quit the app to "
     "update now' will appear, with two buttons: 'Show updates' and 'Don't "
     "remind me again'.",
     "Press the 'Don't remind me again' button.", NULL,
     test_update_available_3},
    {4, "/update_available/test4",
     "A notification with the app store icon (an orange bag with an A), and "
     "the text 'Updates available for 2 apps' 'Test app 4_1 and Test app 4_2 "
     "will update when you quit them.' will appear, with two buttons: 'Show "
     "updates' and 'Don't remind me again'.",
     "Press the 'Don't remind me again' button.", NULL,
     test_update_available_4},
    {5, "/update_available/test5",
     "A notification with the app store icon (an orange bag with an A), and "
     "the text 'Updates available for 3 apps' 'Test app 5_1, Test app 5_2 and "
     "Test app 5_3 "
     "will update when you quit them.' will appear, with two buttons: 'Show "
     "updates' and 'Don't remind me again'.",
     "Press the 'Don't remind me again' button.", NULL,
     test_update_available_5},
    {6, "/update_available/test6",
     "A notification with the app store icon (an orange bag with an A), and "
     "the text 'Updates available for 4 apps' 'Quit the apps to update them "
     "now.' will appear, with two buttons: 'Show "
     "updates' and 'Don't remind me again'.",
     "Press the 'Don't remind me again' button.", NULL,
     test_update_available_6},
    {7, "/update_done/test7",
     "A notification with an icon that consists on a pale pink circle with a T "
     "inside, and the text 'Test app 7 was updated', 'You can reopen it now.' "
     "will appear.",
     "Click on the notification.", NULL, test_update_available_7},
    {8, "/update_forced/test8",
     "A notification with an icon that consists on a pale pink circle with a T "
     "inside, and the text 'Test app 8 will quit and update in 2 days', 'Save "
     "your progress and quit now to prevent data loss' will appear, with two "
     "buttons: 'Show updates' and 'Don't "
     "remind me again'.",
     "Click on the notification.", NULL, test_update_available_8},
    {9, "/update_forced/test9",
     "A notification with an icon that consists on a pale pink circle with a T "
     "inside, and the text 'Test app 9 will quit and update in 2 days', 'Save "
     "your progress and quit now to prevent data loss' will appear, with one "
     "button: 'Show updates'",
     "Click on the 'Show updates' button.", NULL, test_update_available_9},
    {10, "/update_forced/test10",
     "A notification with an icon that consists on a pale pink circle with a T "
     "inside, and the text 'Test app 10 will quit and update in 2 days', 'Save "
     "your progress and quit now to prevent data loss' will appear, with two "
     "buttons: 'Show updates' and 'Don't "
     "remind me again'.",
     "Click on the 'Don't remind me again' button.", NULL,
     test_update_available_10},
    {-1, NULL, NULL, NULL, NULL, NULL}};

/**
 * GApplication callbacks
 */

static void do_startup(GObject *object, gpointer data) {
  notifier = sdi_notify_new(G_APPLICATION(object));
}

static void do_activate(GObject *object, gpointer data) {
  // because, by default, there are no windows, so the application would quit
  g_application_hold(G_APPLICATION(object));

  // add tests
  for (TestData *test = test_data; test->test_number != -1; test++) {
    g_test_add_data_func(test->title, test, (GTestDataFunc)test->test_function);
  }
  g_test_run();
  g_application_release(G_APPLICATION(object));
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  // here we will create any temporary files
  tmpdirpath = g_dir_make_tmp(NULL, NULL);

  g_autoptr(GApplication) app = g_application_new("io.snapcraft.SdiNotifyTest",
                                                  G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "startup", (GCallback)do_startup, NULL);
  g_signal_connect(app, "activate", (GCallback)do_activate, NULL);
  g_application_run(app, argc, argv);
  g_free(tmpdirpath);
}
