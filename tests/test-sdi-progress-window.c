#include "../src/sdi-progress-window.h"
#include "glib-2.0/glib.h"
#include "gtk/gtk.h"
#include <gio/gdesktopappinfo.h>
SdiProgressWindow *progress_window = NULL;
GtkApplicationWindow *window = NULL;
GtkLabel *window_text = NULL;
GtkButton *yes_button = NULL;
GtkButton *no_button = NULL;
static gboolean clicked_on_wait = FALSE;
static gint timeout_id1 = 0;
static gint timeout_id2 = 0;

typedef struct _testData {
  const int test_number;
  const char *title;
  const char *description;
  const char *actions;
  const void (*test_function)(struct _testData *);
} TestData;

gchar *app_list[] = {"kicad_kicad.desktop", "simple-scan_simple-scan.desktop",
                     NULL};

/**
 * Several help functions
 */

static void buttons_callback(GtkButton *button, gchar *answer) {
  g_assert_cmpstr(answer, ==, "yes");
  g_assert_false(clicked_on_wait);
  clicked_on_wait = TRUE;
}

static GtkApplicationWindow *create_window(GApplication *app) {
  GtkApplicationWindow *app_window =
      GTK_APPLICATION_WINDOW(gtk_application_window_new(GTK_APPLICATION(app)));
  window_text = GTK_LABEL(gtk_label_new(""));
  gtk_label_set_wrap(window_text, TRUE);
  gtk_label_set_wrap_mode(window_text, PANGO_WRAP_WORD_CHAR);
  gtk_label_set_max_width_chars(window_text, 80);
  yes_button = GTK_BUTTON(gtk_button_new_with_label("Yes"));
  no_button = GTK_BUTTON(gtk_button_new_with_label("No"));
  GtkBox *box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 5));
  gtk_box_append(box, GTK_WIDGET(window_text));
  gtk_box_append(box, GTK_WIDGET(yes_button));
  gtk_box_append(box, GTK_WIDGET(no_button));
  gtk_window_set_child(GTK_WINDOW(app_window), GTK_WIDGET(box));
  gtk_window_present(GTK_WINDOW(app_window));

  g_signal_connect(yes_button, "clicked", (GCallback)buttons_callback, "yes");
  g_signal_connect(no_button, "clicked", (GCallback)buttons_callback, "no");
  return app_window;
}

static void describe_test(TestData *test) {
  g_assert_nonnull(test);
  g_assert(test->test_number);
  g_autofree gchar *text = g_strdup_printf(
      "Test %d; Title: %s\n\nDescription: %s\n\nActions: %s", test->test_number,
      test->title, test->description, test->actions);
  gtk_label_set_label(window_text, text);
}

static gboolean set_progress_bar(gchar *snap_name) {
  static guint done_tasks = 0;
  guint total_tasks = 10;

  g_autofree gchar *description =
      g_strdup_printf("Description for task %d", done_tasks);
  sdi_progress_window_update_progress(progress_window, snap_name, NULL,
                                      description, done_tasks, total_tasks,
                                      FALSE);
  done_tasks++;
  if (done_tasks > total_tasks)
    done_tasks = 0;
  return G_SOURCE_CONTINUE;
}

static void wait_for_click() {
  GMainContext *context = g_main_context_default();
  clicked_on_wait = FALSE;
  do {
    g_main_context_iteration(context, TRUE);
  } while (!clicked_on_wait);
}

static void enable_buttons(gpointer data) {
  gtk_widget_set_sensitive(GTK_WIDGET(yes_button), TRUE);
  gtk_widget_set_sensitive(GTK_WIDGET(no_button), TRUE);
}

static void disable_buttons_for_seconds(gint seconds) {
  gtk_widget_set_sensitive(GTK_WIDGET(yes_button), FALSE);
  gtk_widget_set_sensitive(GTK_WIDGET(no_button), FALSE);
  g_timeout_add_once(seconds * 1000, enable_buttons, NULL);
}

static void show_progress_window(gchar *snap_name, gchar *desktop_file) {
  g_autoptr(GDesktopAppInfo) app_info = g_desktop_app_info_new(desktop_file);
  g_assert_nonnull(app_info);
  g_autofree gchar *icon = g_desktop_app_info_get_string(app_info, "Icon");
  sdi_progress_window_begin_refresh(
      progress_window, snap_name,
      // GDesktopAppInfo inherits from GAppInfo, so a check is not needed
      (gchar *)g_app_info_get_display_name(G_APP_INFO(app_info)), icon);
}

static int count_progress_childs() {
  GtkWindow *window =
      GTK_WINDOW(sdi_progress_window_get_window(progress_window));
  // to differentiate between window existing and zero elements, and no window
  if (window == NULL)
    return -1;
  GtkWidget *container = gtk_window_get_child(window);
  g_assert_true(GTK_IS_BOX(container));
  GtkWidget *child = gtk_widget_get_first_child(container);
  int counter = 0;
  while (child != NULL) {
    counter++;
    child = gtk_widget_get_next_sibling(child);
  }
  return counter;
}

static int count_hash_childs() {
  GHashTable *table = sdi_progress_window_get_dialogs(progress_window);
  g_assert_nonnull(table);
  return g_hash_table_size(table);
}

// these are the actual tests

static void test_progress_bar(TestData *test) {
  describe_test(test);
  show_progress_window("A-SNAP", *app_list);

  g_assert_cmpint(count_hash_childs(), ==, 1);
  g_assert_cmpint(count_progress_childs(), ==, 1);
  gint timeout_id = g_timeout_add(500, (GSourceFunc)set_progress_bar, "A-SNAP");
  wait_for_click();
  g_source_remove(timeout_id);
}

static void test_pulse_bar(TestData *test) {
  describe_test(test);
  disable_buttons_for_seconds(6);
  g_assert_cmpint(count_hash_childs(), ==, 1);
  g_assert_cmpint(count_progress_childs(), ==, 1);
  wait_for_click();
}

static void test_close_bar(TestData *test) {
  describe_test(test);
  sdi_progress_window_end_refresh(progress_window, "A-SNAP");
  g_assert_cmpint(count_hash_childs(), ==, 0);
  g_assert_cmpint(count_progress_childs(), ==, -1);
  gint timeout_id = g_timeout_add(500, (GSourceFunc)set_progress_bar, "A-SNAP");
  wait_for_click();
  g_assert_cmpint(count_hash_childs(), ==, 0);
  g_assert_cmpint(count_progress_childs(), ==, -1);
  g_source_remove(timeout_id);
}

static void test_manual_hide(TestData *test) {
  describe_test(test);
  show_progress_window("B-SNAP", *(app_list + 1));
  g_assert_cmpint(count_hash_childs(), ==, 1);
  g_assert_cmpint(count_progress_childs(), ==, 1);
  gint timeout_id = g_timeout_add(500, (GSourceFunc)set_progress_bar, "B-SNAP");
  wait_for_click();
  g_assert_cmpint(count_hash_childs(), ==, 0);
  g_assert_cmpint(count_progress_childs(), ==, -1);
  g_source_remove(timeout_id);
}

static void test_dual_progress_bar1(TestData *test) {
  describe_test(test);
  show_progress_window("C-SNAP", *(app_list));
  g_assert_cmpint(count_hash_childs(), ==, 1);
  g_assert_cmpint(count_progress_childs(), ==, 1);
  timeout_id1 = g_timeout_add(500, (GSourceFunc)set_progress_bar, "C-SNAP");
  wait_for_click();
}

static void test_dual_progress_bar2(TestData *test) {
  describe_test(test);
  show_progress_window("D-SNAP", *(app_list + 1));
  g_assert_cmpint(count_hash_childs(), ==, 2);
  g_assert_cmpint(count_progress_childs(), ==, 2);
  timeout_id2 = g_timeout_add(300, (GSourceFunc)set_progress_bar, "D-SNAP");
  wait_for_click();
}

static void test_dual_progress_bar3(TestData *test) {
  describe_test(test);
  sdi_progress_window_end_refresh(progress_window, "C-SNAP");
  g_assert_cmpint(count_hash_childs(), ==, 1);
  g_assert_cmpint(count_progress_childs(), ==, 1);
  wait_for_click();
  g_source_remove(timeout_id1);
}

static void test_dual_progress_bar4(TestData *test) {
  describe_test(test);
  sdi_progress_window_end_refresh(progress_window, "D-SNAP");
  g_assert_cmpint(count_hash_childs(), ==, 0);
  g_assert_cmpint(count_progress_childs(), ==, -1);
  wait_for_click();
  g_source_remove(timeout_id2);
}

/**
 * Each test must have an entry in test_data
 */

TestData test_data[] = {
    {1, "/progress_window/test_progress_bar",
     "A window with a progress bar must appear with the name and icon of the "
     "KiCad application, growing from 1 to 10 and going back to 1 "
     "periodically. It must not 'pulse' (move left-to-right and then "
     "right-to-left softly).\n"
     "The text in the progress bar should be 'Description for task X (X/10)', "
     "being X a number between 1 and 10.\nIf no window is shown, check if it "
     "is below this window.",
     "Confirm that is correct.", test_progress_bar},
    {2, "/progress_window/test_pulsing_bar",
     "The same window must remain, with the progress bar frozen. Wait for 5-7 "
     "seconds, and the progress bar must begin 'pulsing' (moving left-to-right "
     "and then "
     "right-to-left softly)\nThe YES and NO buttons will be disabled for 5 "
     "seconds, so wait at least until they are re-enabled.",
     "Confirm that is correct.", test_pulse_bar},
    {3, "/progress_window/test_close_bar",
     "The window must disappear and no new window should appear (check below "
     "this window just in case).\n",
     "Confirm that is correct.", test_close_bar},
    {4, "/progress_window/test_manual_hide",
     "A window with a progress bar must appear with the name and icon of the "
     "Simple Scan application.",
     "Click on the 'Hide' button. The window must "
     "disappear and no new window should appear (check below this one). "
     "Confirm that is correct.",
     test_manual_hide},
    {5, "/progress_window/test_dual_progress_bar_1",
     "A window with a progress bar must appear with the name and icon of the "
     "KiCad application, growing from 1 to 10 and going back to 1 "
     "periodically. It must not 'pulse' (move left-to-right and then "
     "right-to-left softly).\n"
     "The text in the progress bar should be 'Description for task X (X/10)', "
     "being X a number between 1 and 10.\nIf no window is shown, check if it "
     "is below this window.",
     "Confirm that is correct.", test_dual_progress_bar1},
    {6, "/progress_window/test_dual_progress_bar_2",
     "A second progress bar must appear in the same window with the name and "
     "icon of the "
     "Simple Scan application, growing from 1 to 10 and going back to 1 "
     "periodically. It must not 'pulse' (move left-to-right and then "
     "right-to-left softly).\n"
     "The text in the progress bar should be 'Description for task X (X/10)', "
     "being X a number between 1 and 10.\nIf no window is shown, check if it "
     "is below this window.",
     "Confirm that is correct.", test_dual_progress_bar2},
    {7, "/progress_window/test_dual_progress_bar_3",
     "The first progress bar must disappear, remaining only the second "
     "progress bar. Neither a new window, nor a new progress bar, should "
     "appear (check below "
     "this window just in case).\n",
     "Confirm that is correct.", test_dual_progress_bar3},
    {8, "/progress_window/test_dual_progress_bar_4",
     "The window must disappear and no new window should appear (check below "
     "this window just in case).\n",
     "Confirm that is correct.", test_dual_progress_bar4},
    {0, NULL, NULL, NULL, NULL}};

/**
 * GApplication callbacks
 */

static void do_startup(GObject *object, gpointer data) {
  progress_window = sdi_progress_window_new(G_APPLICATION(object));

  g_assert_nonnull(app_list);
}

static void do_activate(GApplication *app, gpointer data) {
  window = create_window(app);

  // add tests
  for (TestData *test = test_data; test->test_number; test++) {
    g_test_add_data_func(test->title, test, (GTestDataFunc)test->test_function);
  }
  g_test_run();
  g_object_unref(window);
  g_object_unref(progress_window);
}

static gchar *get_data_path() {
  g_autofree gchar *path = g_test_build_filename(G_TEST_BUILT, "data", NULL);
  return g_canonicalize_filename(path, NULL);
}

void set_environment() {
  const gchar *data_dirs = g_getenv("XDG_DATA_DIRS");
  g_autofree gchar *share_path = get_data_path();
  g_autofree gchar *newvar = g_strdup_printf("%s:%s", share_path, data_dirs);
  g_setenv("XDG_DATA_DIRS", newvar, TRUE);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  set_environment();

  g_autoptr(GApplication) app = G_APPLICATION(gtk_application_new(
      "io.snapcraft.SdiProgressDockTest", G_APPLICATION_DEFAULT_FLAGS));
  g_signal_connect(app, "startup", (GCallback)do_startup, NULL);
  g_signal_connect(app, "activate", (GCallback)do_activate, NULL);
  g_application_run(app, argc, argv);
}
