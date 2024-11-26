#include "../src/sdi-progress-dock.h"
#include "gtk/gtk.h"
SdiProgressDock *progress_dock = NULL;
GtkApplicationWindow *window = NULL;
GtkLabel *window_text = NULL;
GtkButton *yes_button = NULL;
GtkButton *no_button = NULL;
static GMainLoop *loop = NULL;

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

static gboolean set_progress_bar(gchar *desktop_file) {
  static guint done_tasks = 0;
  guint total_tasks = 10;

  g_autoptr(GStrvBuilder) desktop_files_builder = g_strv_builder_new();
  g_strv_builder_add(desktop_files_builder, desktop_file);
  g_auto(GStrv) desktop_files = g_strv_builder_end(desktop_files_builder);
  sdi_progress_dock_update_progress(progress_dock, "snap name", desktop_files,
                                    "a description", done_tasks, total_tasks,
                                    FALSE);
  done_tasks++;
  if (done_tasks >= total_tasks)
    done_tasks = 0;
  return G_SOURCE_CONTINUE;
}

static void wait_for_click() {
  loop = g_main_loop_new(NULL, FALSE);
  g_main_loop_run(loop);
  g_main_loop_unref(loop);
}

// these are the actual tests

static void test_progress_bar(TestData *test) {
  describe_test(test);
  gint timeout_id =
      g_timeout_add(200, (GSourceFunc)set_progress_bar, *app_list);
  wait_for_click();
  g_source_remove(timeout_id);
}

static void test_launch_forbidden(TestData *test) {
  describe_test(test);
  gint timeout_id =
      g_timeout_add(200, (GSourceFunc)set_progress_bar, *(app_list + 1));
  wait_for_click();
  g_source_remove(timeout_id);
}

static void test_launch_is_enabled(TestData *test) {
  describe_test(test);
  sdi_progress_dock_update_progress(progress_dock, "snap name", app_list,
                                    "a description", 7, 10, TRUE);
  sdi_progress_dock_update_progress(progress_dock, "snap name2", app_list + 1,
                                    "a description2", 10, 10, TRUE);
  wait_for_click();
}

/**
 * Each test must have an entry in test_data
 */

TestData test_data[] = {
    {1, "/dock/test_progress_bar",
     "A progress bar must appear in the first icon, "
     "growing and shrinking periodically. The icon"
     "must be 'grayed out', to indicate it is disabled",
     "Confirm that is correct.", test_progress_bar},

    {2, "/dock/test_launch_forbidden",
     "A progress bar must appear in the second icon, "
     "growing and shrinking periodically. Ensure that the app isn't running "
     "(close it if needed).",
     "Click on the icon and confirm that the application"
     " isn't launch, and a popup with appears saying that"
     " the application is updating",
     test_launch_forbidden},

    {3, "/dock/test_launch_is_enabled",
     "No progress bar should appear in neither the first, nor the second "
     "icons, and clicking on them must launch the app",
     "Confirm that is correct", test_launch_is_enabled},

    {-1, NULL, NULL, NULL, NULL}};

/**
 * GApplication callbacks
 */

static void do_startup(GObject *object, gpointer data) {
  progress_dock = sdi_progress_dock_new(G_APPLICATION(object));

  // this gsettings key stores the path to the .desktop files of the programs
  // in the dock. Since we need to test a progress bar in the dock, we need
  // that list.
  g_autoptr(GSettings) settings = g_settings_new("org.gnome.shell");
  app_list = g_settings_get_strv(settings, "favorite-apps");
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
