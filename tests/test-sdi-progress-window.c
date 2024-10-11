#include "../src/sdi-progress-window.h"
#include "glib-2.0/glib.h"
#include "gtk/gtk.h"
#include <gio/gdesktopappinfo.h>
SdiProgressWindow *progress_window = NULL;
GtkApplicationWindow *window = NULL;
GtkLabel *window_text = NULL;
GtkButton *yes_button = NULL;
GtkButton *no_button = NULL;
static GMainLoop *loop = NULL;
static gint timeout_id1 = 0;
static gint timeout_id2 = 0;

typedef struct _testData {
  const int test_number;
  const char *title;
  const char *description;
  const char *actions;
  const void (*test_function)(struct _testData *);
} TestData;

gchar **app_list = NULL;

/**
 * Several help functions
 */

static void buttons_callback(GtkButton *button, gchar *answer) {
  g_assert_cmpstr(answer, ==, "yes");
  g_main_loop_quit(loop);
}

static void create_window(GApplication *app) {
  window =
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
  gtk_window_set_child(GTK_WINDOW(window), GTK_WIDGET(box));
  gtk_window_present(GTK_WINDOW(window));

  g_signal_connect(yes_button, "clicked", (GCallback)buttons_callback, "yes");
  g_signal_connect(no_button, "clicked", (GCallback)buttons_callback, "no");
}

static void describe_test(TestData *test) {
  g_assert_nonnull(test);
  g_assert_cmpint(test->test_number, !=, -1);
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
                                      FALSE, NULL);
  done_tasks++;
  if (done_tasks > total_tasks)
    done_tasks = 0;
  return G_SOURCE_CONTINUE;
}

static void wait_for_click() {
  loop = g_main_loop_new(NULL, FALSE);
  g_main_loop_run(loop);
  g_main_loop_unref(loop);
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
  sdi_progress_window_begin_refresh(
      progress_window, snap_name,
      (gchar *)g_app_info_get_display_name(G_APP_INFO(app_info)),
      g_desktop_app_info_get_string(app_info, "Icon"), NULL);
}

// these are the actual tests

static void test_progress_bar(TestData *test) {
  describe_test(test);
  show_progress_window("A-SNAP", *app_list);
  gint timeout_id = g_timeout_add(500, (GSourceFunc)set_progress_bar, "A-SNAP");
  wait_for_click();
  g_source_remove(timeout_id);
}

static void test_pulse_bar(TestData *test) {
  describe_test(test);
  disable_buttons_for_seconds(6);
  wait_for_click();
}

static void test_close_bar(TestData *test) {
  describe_test(test);
  sdi_progress_window_end_refresh(progress_window, "A-SNAP", NULL);
  gint timeout_id = g_timeout_add(500, (GSourceFunc)set_progress_bar, "A-SNAP");
  wait_for_click();
  g_source_remove(timeout_id);
}

static void test_manual_hide(TestData *test) {
  describe_test(test);
  show_progress_window("B-SNAP", *(app_list + 1));
  gint timeout_id = g_timeout_add(500, (GSourceFunc)set_progress_bar, "B-SNAP");
  wait_for_click();
  g_source_remove(timeout_id);
}

static void test_dual_progress_bar1(TestData *test) {
  describe_test(test);
  show_progress_window("C-SNAP", *(app_list + 2));
  timeout_id1 = g_timeout_add(500, (GSourceFunc)set_progress_bar, "C-SNAP");
  wait_for_click();
}

static void test_dual_progress_bar2(TestData *test) {
  describe_test(test);
  show_progress_window("D-SNAP", *(app_list + 3));
  timeout_id2 = g_timeout_add(300, (GSourceFunc)set_progress_bar, "D-SNAP");
  wait_for_click();
}

static void test_dual_progress_bar3(TestData *test) {
  describe_test(test);
  sdi_progress_window_end_refresh(progress_window, "C-SNAP", NULL);
  wait_for_click();
  g_source_remove(timeout_id1);
}

static void test_dual_progress_bar4(TestData *test) {
  describe_test(test);
  sdi_progress_window_end_refresh(progress_window, "D-SNAP", NULL);
  wait_for_click();
  g_source_remove(timeout_id2);
}

/**
 * Each test must have an entry in test_data
 */

TestData test_data[] = {
    {1, "/progress_window/test_progress_bar",
     "A window with a progress bar must appear with the name and icon of a "
     "snapped application, growing from 1 to 10 and going back to 1 "
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
     "A window with a progress bar must appear with the name and icon of a "
     "snapped application.",
     "Click on the 'Hide' button. The window must "
     "disappear and no new window should appear (check below this one). "
     "Confirm that is correct.",
     test_manual_hide},
    {5, "/progress_window/test_dual_progress_bar_1",
     "A window with a progress bar must appear with the name and icon of a "
     "snapped application, growing from 1 to 10 and going back to 1 "
     "periodically. It must not 'pulse' (move left-to-right and then "
     "right-to-left softly).\n"
     "The text in the progress bar should be 'Description for task X (X/10)', "
     "being X a number between 1 and 10.\nIf no window is shown, check if it "
     "is below this window.",
     "Confirm that is correct.", test_dual_progress_bar1},
    {6, "/progress_window/test_dual_progress_bar_2",
     "A second progress bar must appear in the same window with the name and "
     "icon of a "
     "snapped application, growing from 1 to 10 and going back to 1 "
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
    {-1, NULL, NULL, NULL, NULL}};

/**
 * GApplication callbacks
 */

static void do_startup(GObject *object, gpointer data) {
  progress_window = sdi_progress_window_new(G_APPLICATION(object));

  // this gsettings key stores the path to the .desktop files of the programs
  // in the dock. Since we need to test a progress bar in the dock, we need
  // that list.
  g_autoptr(GSettings) settings = g_settings_new("org.gnome.shell");
  GList *applist = g_app_info_get_all();
  g_autoptr(GStrvBuilder) builder = g_strv_builder_new();
  // make a list with only snapped icons
  for (GList *p = applist; p != NULL; p = p->next) {
    GAppInfo *app_info = G_APP_INFO(p->data);
    if (!g_app_info_should_show(app_info)) {
      continue;
    }
    const gchar *command_line = g_app_info_get_commandline(app_info);
    if (strstr(command_line, "/snapd/") != NULL) {
      g_strv_builder_add(builder, g_app_info_get_id(p->data));
    }
  }
  app_list = g_strv_builder_end(builder);
}

static void do_activate(GApplication *app, gpointer data) {
  create_window(app);

  // add tests
  for (TestData *test = test_data; test->test_number != -1; test++) {
    g_test_add_data_func(test->title, test, (GTestDataFunc)test->test_function);
  }
  g_test_run();
  g_object_unref(window);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);

  g_autoptr(GApplication) app = G_APPLICATION(gtk_application_new(
      "io.snapcraft.SdiProgressDockTest", G_APPLICATION_DEFAULT_FLAGS));
  g_signal_connect(app, "startup", (GCallback)do_startup, NULL);
  g_signal_connect(app, "activate", (GCallback)do_activate, NULL);
  g_application_run(app, argc, argv);
}
