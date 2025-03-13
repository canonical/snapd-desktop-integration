#include "../src/sdi-progress-window.h"
#include "../src/sdi-refresh-dialog.h"
#include "glib-2.0/glib.h"
#include "gtk/gtk.h"
#include <gio/gdesktopappinfo.h>
SdiProgressWindow *progress_window = NULL;
static gboolean timeout_expired = FALSE;

gchar *app_list[] = {"kicad-no-icon_kicad-no-icon.desktop",
                     "simple-scan_simple-scan.desktop", NULL};

/**
 * Several help functions
 */

static void set_progress_bar(gchar *snap_name, guint done_tasks,
                             guint total_tasks) {
  g_autofree gchar *description =
      g_strdup_printf("Description for task %d", done_tasks);
  sdi_progress_window_update_progress(progress_window, snap_name, NULL,
                                      description, done_tasks, total_tasks,
                                      FALSE);
}

static void expire_timeout(gpointer data) { timeout_expired = TRUE; }

static void wait_for_timeout(guint seconds) {
  guint mseconds = seconds * 1000;
  if (mseconds == 0) {
    mseconds = 100;
  }
  GMainContext *context = g_main_context_default();
  timeout_expired = FALSE;
  g_timeout_add_once(mseconds, expire_timeout, NULL);
  do {
    g_main_context_iteration(context, TRUE);
  } while (!timeout_expired);
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

/**
 * Counts how many SdiRefreshDialogs are inside the progress window.
 * This is done inside Gtk itself, thus ensuring that the desired
 * widgets have been created.
 */
static int count_progress_childs() {
  GtkWindow *window =
      GTK_WINDOW(sdi_progress_window_get_window(progress_window));
  // to differentiate between window existing and zero elements, and no window
  if (window == NULL) {
    return -1;
  }
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

/**
 * Find recursively all the widgets of the specified type
 * This is done inside Gtk itself, thus ensuring that the desired
 * widgets have been created.
 */
static GSList *find_widgets_by_type(GtkWidget *widget, GType type) {
  GSList *widget_list = NULL;

  if (widget == NULL) {
    return NULL;
  }

  if (G_TYPE_CHECK_INSTANCE_TYPE(widget, SDI_TYPE_REFRESH_DIALOG)) {
    widget = gtk_widget_get_first_child(widget);
    if (widget == NULL) {
      return NULL;
    }
  }
  if (G_TYPE_CHECK_INSTANCE_TYPE(widget, type)) {
    widget_list = g_slist_append(widget_list, widget);
  }
  GtkWidget *next_widget = gtk_widget_get_next_sibling(widget);
  // g_slist_concat frees new_list, so no g_autoptr is required
  GSList *new_list = find_widgets_by_type(next_widget, type);
  if (new_list != NULL) {
    widget_list = g_slist_concat(widget_list, new_list);
  }

  next_widget = gtk_widget_get_first_child(widget);
  new_list = find_widgets_by_type(next_widget, type);
  if (new_list != NULL) {
    widget_list = g_slist_concat(widget_list, new_list);
  }

  return widget_list;
}

/**
 * Searches inside a SdiRefreshDialog for a label with the refresh description
 * text for the specified program name.
 * This is done inside Gtk itself, thus ensuring that the desired
 * widgets have been created.
 */
static GtkWidget *find_progress_by_description(const gchar *program_name) {
  GtkWindow *window =
      GTK_WINDOW(sdi_progress_window_get_window(progress_window));

  if (window == NULL)
    return NULL;

  GtkWidget *container = gtk_window_get_child(window);
  g_assert_true(GTK_IS_BOX(container));

  GtkWidget *child = gtk_widget_get_first_child(container);
  while (child != NULL) {
    g_autoptr(GSList) labels = find_widgets_by_type(child, GTK_TYPE_LABEL);
    for (GSList *labelp = labels; labelp != NULL; labelp = labelp->next) {
      GtkWidget *label = (GtkWidget *)labelp->data;
      const gchar *label_description = gtk_label_get_label(GTK_LABEL(label));
      g_autofree gchar *full_desc =
          g_strdup_printf("Updating %s to the latest version.", program_name);
      if (g_strcmp0(full_desc, label_description) == 0) {
        return child;
      }
    }
    child = gtk_widget_get_next_sibling(child);
  }
  return NULL;
}

static int count_hash_childs() {
  GHashTable *table = sdi_progress_window_get_dialogs(progress_window);
  g_assert_nonnull(table);
  return g_hash_table_size(table);
}

/**
 * Searches inside a SdiRefreshDialog for a progress bar with the
 * expected value and text for the specified number total of tasks,
 * and number of done tasks.
 * This is done inside Gtk itself, thus ensuring that the desired
 * widgets have been created.
 */
static bool check_progress_status(GtkWidget *element, guint done_tasks,
                                  guint total_tasks) {
  g_autofree gchar *description = g_strdup_printf(
      "Description for task %d (%d/%d)", done_tasks, done_tasks, total_tasks);
  g_autoptr(GSList) progress_bars =
      find_widgets_by_type(element, GTK_TYPE_PROGRESS_BAR);
  if (progress_bars == NULL) {
    g_print("Progress bar is NULL\n");
    return false;
  }
  if (g_slist_length(progress_bars) != 1) {
    g_print("There are more than one progress bar\n");
    return false;
  }
  GtkProgressBar *progress_bar = (GtkProgressBar *)progress_bars->data;
  const gchar *bar_text = gtk_progress_bar_get_text(progress_bar);
  if (bar_text == NULL) {
    g_print("Progress bar text is NULL\n");
    return false;
  }
  if (g_strcmp0(bar_text, description) != 0) {
    g_print("Progress var text doesn't match. Expected %s, obtained %s\n",
            description, bar_text);
    return false;
  }
  gdouble fraction = ((gdouble)done_tasks) / ((gdouble)total_tasks);

  gdouble actual_fraction = gtk_progress_bar_get_fraction(progress_bar);
  if (!G_APPROX_VALUE(fraction, actual_fraction, DBL_EPSILON)) {
    g_print("Progress bar value incorrect: expected %f, obtained %f\n",
            fraction, actual_fraction);
    return false;
  }
  return true;
}

typedef enum {

  VISIBILITY_LEVEL_NO_WINDOW,
  VISIBILITY_LEVEL_WINDOW_NOT_VISIBLE,
  VISIBILITY_LEVEL_CONTAINER_NOT_VISIBLE,
  VISIBILITY_LEVEL_ELEMENT_NOT_VISIBLE,
  VISIBILITY_LEVEL_ALL_VISIBLE,
} VisibilityLevel;

/* returns the visibility level of the widgets inside the progress window:
 * 0: top-level window doesn't exist
 * 1: top-level window exists, but is not visible
 * 2: container is not visible
 * 3: any of the progress status elements is not visible
 * 4: all the elements are visible
 */
static VisibilityLevel check_full_visibility() {
  GtkWindow *window =
      GTK_WINDOW(sdi_progress_window_get_window(progress_window));
  // to differentiate between window existing and zero elements, and no window
  if (window == NULL) {
    return VISIBILITY_LEVEL_NO_WINDOW;
  }
  if (!gtk_widget_get_visible(GTK_WIDGET(window))) {
    return VISIBILITY_LEVEL_WINDOW_NOT_VISIBLE;
  }
  GtkWidget *container = gtk_window_get_child(window);
  g_assert_true(GTK_IS_BOX(container));
  if (!gtk_widget_get_visible(GTK_WIDGET(container))) {
    return VISIBILITY_LEVEL_CONTAINER_NOT_VISIBLE;
  }
  GtkWidget *child = gtk_widget_get_first_child(container);
  while (child != NULL) {
    if (!gtk_widget_get_visible(GTK_WIDGET(child))) {
      return VISIBILITY_LEVEL_ELEMENT_NOT_VISIBLE;
    }
    child = gtk_widget_get_next_sibling(child);
  }
  return VISIBILITY_LEVEL_ALL_VISIBLE;
}

typedef enum {
  LABEL_VISIBLE = 1,
  ICON_VISIBLE = 2,
  BUTTON_VISIBLE = 4,
  PROGRESS_BAR_VISIBLE = 8,
} Visibility;

#define ALL_VISIBLE                                                            \
  LABEL_VISIBLE + ICON_VISIBLE + BUTTON_VISIBLE + PROGRESS_BAR_VISIBLE
#define ALL_BUT_ICON_VISIBLE                                                   \
  LABEL_VISIBLE + BUTTON_VISIBLE + PROGRESS_BAR_VISIBLE

static bool check_widget_type_visibility(GtkWidget *element, GType type) {
  g_autoptr(GSList) items = find_widgets_by_type(element, type);
  for (GSList *item = items; item != NULL; item = item->next) {
    GtkWidget *widget = (GtkWidget *)item->data;
    if ((widget != NULL) && (!gtk_widget_get_visible(widget))) {
      return false;
    }
  }
  return true;
}

/**
 * Returns the visibility level of each possible widget inside a
 * SdiRefreshDialog. This is done inside Gtk itself, thus ensuring that the
 * status is really the expected one.
 */
static Visibility check_widgets_visibility(GtkWidget *element) {
  Visibility status = ALL_VISIBLE;

  if (!check_widget_type_visibility(element, GTK_TYPE_LABEL)) {
    status &= ~LABEL_VISIBLE;
  }
  if (!check_widget_type_visibility(element, GTK_TYPE_IMAGE)) {
    status &= ~ICON_VISIBLE;
  }
  if (!check_widget_type_visibility(element, GTK_TYPE_PROGRESS_BAR)) {
    status &= ~PROGRESS_BAR_VISIBLE;
  }
  if (!check_widget_type_visibility(element, GTK_TYPE_BUTTON)) {
    status &= ~BUTTON_VISIBLE;
  }
  return status;
}

typedef enum {
  PROGRESS_BAR_PULSING,
  PROGRESS_BAR_VALUE,
  PROGRESS_BAR_NULL,
  PROGRESS_BAR_MORE_THAN_ONE,
} ProgressBarStatus;

/**
 * Returns wether a progress bar in a SdiRefreshDialog is in pulse or
 * value mode.
 * This is done inside Gtk itself, thus ensuring that the status is really the
 * expected one.
 */
static ProgressBarStatus progress_bar_pulse_status(GtkWidget *element) {
  g_autoptr(GSList) progress_bars =
      find_widgets_by_type(element, GTK_TYPE_PROGRESS_BAR);

  if (progress_bars == NULL) {
    return PROGRESS_BAR_NULL;
  }
  if (g_slist_length(progress_bars) != 1) {
    return PROGRESS_BAR_MORE_THAN_ONE;
  }

  if (GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(progress_bars->data),
                                         "pulsed_progress_bar")) == 1) {
    return PROGRESS_BAR_PULSING;
  }
  return PROGRESS_BAR_VALUE;
}

/**
 * Emulates clicking on the HIDE button of a SdiRefreshDialog.
 */
static bool press_hide_button(GtkWidget *element) {
  g_autoptr(GSList) buttons = find_widgets_by_type(element, GTK_TYPE_BUTTON);

  if (g_slist_length(buttons) != 1) {
    return false;
  }

  gtk_widget_activate((GtkWidget *)(buttons->data));
  return true;
}

// these are the actual tests

static void test_progress_bar() {
  g_assert_cmpint(count_hash_childs(), ==, 0);
  g_assert_cmpint(count_progress_childs(), ==, -1);

  show_progress_window("A-SNAP", *app_list);

  wait_for_timeout(1);
  g_assert_cmpint(count_hash_childs(), ==, 1);
  g_assert_cmpint(count_progress_childs(), ==, 1);

  GtkWidget *element = find_progress_by_description("KiCad-no-icon");
  g_assert_nonnull(element);

  g_assert_cmpint(check_full_visibility(), ==, VISIBILITY_LEVEL_ALL_VISIBLE);
  g_assert_cmpint(check_widgets_visibility(element), ==, ALL_BUT_ICON_VISIBLE);

  set_progress_bar("A-SNAP", 0, 10);
  g_assert_true(check_progress_status(element, 0, 10));
  g_assert_cmpint(progress_bar_pulse_status(element), ==, PROGRESS_BAR_VALUE);

  g_assert_cmpint(check_full_visibility(), ==, VISIBILITY_LEVEL_ALL_VISIBLE);
  g_assert_cmpint(check_widgets_visibility(element), ==, ALL_BUT_ICON_VISIBLE);

  set_progress_bar("A-SNAP", 4, 10);
  g_assert_true(check_progress_status(element, 4, 10));
  g_assert_cmpint(progress_bar_pulse_status(element), ==, PROGRESS_BAR_VALUE);

  g_assert_cmpint(check_full_visibility(), ==, VISIBILITY_LEVEL_ALL_VISIBLE);
  g_assert_cmpint(check_widgets_visibility(element), ==, ALL_BUT_ICON_VISIBLE);

  set_progress_bar("A-SNAP", 10, 10);
  g_assert_true(check_progress_status(element, 10, 10));
  g_assert_cmpint(progress_bar_pulse_status(element), ==, PROGRESS_BAR_VALUE);

  g_assert_cmpint(check_full_visibility(), ==, VISIBILITY_LEVEL_ALL_VISIBLE);
  g_assert_cmpint(check_widgets_visibility(element), ==, ALL_BUT_ICON_VISIBLE);
}

static void test_pulse_bar() {
  g_assert_cmpint(count_hash_childs(), ==, 1);
  g_assert_cmpint(count_progress_childs(), ==, 1);

  GtkWidget *element = find_progress_by_description("KiCad-no-icon");
  g_assert_nonnull(element);

  g_assert_cmpint(check_full_visibility(), ==, VISIBILITY_LEVEL_ALL_VISIBLE);
  g_assert_cmpint(check_widgets_visibility(element), ==, ALL_BUT_ICON_VISIBLE);

  set_progress_bar("A-SNAP", 1, 10);
  g_assert_cmpint(progress_bar_pulse_status(element), ==, PROGRESS_BAR_VALUE);
  wait_for_timeout(7);
  g_assert_cmpint(count_hash_childs(), ==, 1);
  g_assert_cmpint(count_progress_childs(), ==, 1);
  g_assert_cmpint(progress_bar_pulse_status(element), ==, PROGRESS_BAR_PULSING);

  g_assert_cmpint(check_full_visibility(), ==, VISIBILITY_LEVEL_ALL_VISIBLE);
  g_assert_cmpint(check_widgets_visibility(element), ==, ALL_BUT_ICON_VISIBLE);
}

static void test_close_bar() {
  GtkWidget *element = find_progress_by_description("KiCad-no-icon");
  g_assert_nonnull(element);

  g_assert_cmpint(check_full_visibility(), ==, VISIBILITY_LEVEL_ALL_VISIBLE);
  g_assert_cmpint(check_widgets_visibility(element), ==, ALL_BUT_ICON_VISIBLE);

  sdi_progress_window_end_refresh(progress_window, "A-SNAP");
  wait_for_timeout(0);
  g_assert_cmpint(count_hash_childs(), ==, 0);
  g_assert_cmpint(count_progress_childs(), ==, -1);
}

static void test_manual_hide() {
  show_progress_window("B-SNAP", *(app_list + 1));

  GtkWidget *element = find_progress_by_description("Document Scanner");
  g_assert_nonnull(element);

  g_assert_cmpint(check_full_visibility(), ==, VISIBILITY_LEVEL_ALL_VISIBLE);
  g_assert_cmpint(check_widgets_visibility(element), ==, ALL_VISIBLE);
  g_assert_cmpint(count_hash_childs(), ==, 1);
  g_assert_cmpint(count_progress_childs(), ==, 1);

  g_assert_true(press_hide_button(element));
  wait_for_timeout(2);

  g_assert_cmpint(count_hash_childs(), ==, 0);
  g_assert_cmpint(count_progress_childs(), ==, -1);
}

static void test_dual_progress_bar1() {
  show_progress_window("C-SNAP", *(app_list));
  g_assert_cmpint(count_hash_childs(), ==, 1);
  g_assert_cmpint(count_progress_childs(), ==, 1);

  GtkWidget *element = find_progress_by_description("KiCad-no-icon");
  g_assert_nonnull(element);

  g_assert_cmpint(check_full_visibility(), ==, VISIBILITY_LEVEL_ALL_VISIBLE);
  g_assert_cmpint(check_widgets_visibility(element), ==, ALL_BUT_ICON_VISIBLE);

  set_progress_bar("C-SNAP", 0, 10);
  g_assert_true(check_progress_status(element, 0, 10));
  g_assert_cmpint(progress_bar_pulse_status(element), ==, PROGRESS_BAR_VALUE);

  g_assert_cmpint(check_full_visibility(), ==, VISIBILITY_LEVEL_ALL_VISIBLE);
  g_assert_cmpint(check_widgets_visibility(element), ==, ALL_BUT_ICON_VISIBLE);

  set_progress_bar("C-SNAP", 4, 10);
  g_assert_true(check_progress_status(element, 4, 10));
  g_assert_cmpint(progress_bar_pulse_status(element), ==, PROGRESS_BAR_VALUE);

  g_assert_cmpint(check_full_visibility(), ==, VISIBILITY_LEVEL_ALL_VISIBLE);
  g_assert_cmpint(check_widgets_visibility(element), ==, ALL_BUT_ICON_VISIBLE);
}

static void test_dual_progress_bar2() {
  show_progress_window("D-SNAP", *(app_list + 1));
  g_assert_cmpint(count_hash_childs(), ==, 2);
  g_assert_cmpint(count_progress_childs(), ==, 2);

  GtkWidget *element1 = find_progress_by_description("KiCad-no-icon");
  g_assert_nonnull(element1);
  GtkWidget *element2 = find_progress_by_description("Document Scanner");
  g_assert_nonnull(element2);

  g_assert_cmpint(check_full_visibility(), ==, VISIBILITY_LEVEL_ALL_VISIBLE);
  g_assert_cmpint(check_widgets_visibility(element1), ==, ALL_BUT_ICON_VISIBLE);
  g_assert_cmpint(check_widgets_visibility(element2), ==, ALL_VISIBLE);

  wait_for_timeout(1);
  set_progress_bar("C-SNAP", 2, 10);
  g_assert_true(check_progress_status(element1, 2, 10));
  g_assert_cmpint(progress_bar_pulse_status(element1), ==, PROGRESS_BAR_VALUE);

  set_progress_bar("D-SNAP", 1, 10);
  g_assert_true(check_progress_status(element1, 2, 10));
  g_assert_cmpint(progress_bar_pulse_status(element1), ==, PROGRESS_BAR_VALUE);
  g_assert_true(check_progress_status(element2, 1, 10));
  g_assert_cmpint(progress_bar_pulse_status(element2), ==, PROGRESS_BAR_VALUE);

  g_assert_cmpint(check_full_visibility(), ==, VISIBILITY_LEVEL_ALL_VISIBLE);
  g_assert_cmpint(check_widgets_visibility(element1), ==, ALL_BUT_ICON_VISIBLE);
  g_assert_cmpint(check_widgets_visibility(element2), ==, ALL_VISIBLE);

  set_progress_bar("C-SNAP", 4, 10);
  g_assert_true(check_progress_status(element1, 4, 10));
  g_assert_cmpint(progress_bar_pulse_status(element1), ==, PROGRESS_BAR_VALUE);
  g_assert_true(check_progress_status(element2, 1, 10));
  g_assert_cmpint(progress_bar_pulse_status(element2), ==, PROGRESS_BAR_VALUE);

  g_assert_cmpint(check_full_visibility(), ==, VISIBILITY_LEVEL_ALL_VISIBLE);
  g_assert_cmpint(check_widgets_visibility(element1), ==, ALL_BUT_ICON_VISIBLE);
  g_assert_cmpint(check_widgets_visibility(element2), ==, ALL_VISIBLE);

  set_progress_bar("D-SNAP", 7, 10);
  g_assert_true(check_progress_status(element1, 4, 10));
  g_assert_cmpint(progress_bar_pulse_status(element1), ==, PROGRESS_BAR_VALUE);
  g_assert_true(check_progress_status(element2, 7, 10));
  g_assert_cmpint(progress_bar_pulse_status(element2), ==, PROGRESS_BAR_VALUE);

  g_assert_cmpint(check_full_visibility(), ==, VISIBILITY_LEVEL_ALL_VISIBLE);
  g_assert_cmpint(check_widgets_visibility(element1), ==, ALL_BUT_ICON_VISIBLE);
  g_assert_cmpint(check_widgets_visibility(element2), ==, ALL_VISIBLE);
}

static void test_dual_progress_bar3() {
  sdi_progress_window_end_refresh(progress_window, "C-SNAP");
  g_assert_cmpint(count_hash_childs(), ==, 1);
  g_assert_cmpint(count_progress_childs(), ==, 1);
  GtkWidget *element = find_progress_by_description("Document Scanner");
  g_assert_nonnull(element);

  g_assert_true(check_progress_status(element, 7, 10));
  g_assert_cmpint(progress_bar_pulse_status(element), ==, PROGRESS_BAR_VALUE);
  g_assert_cmpint(check_full_visibility(), ==, VISIBILITY_LEVEL_ALL_VISIBLE);
  g_assert_cmpint(check_widgets_visibility(element), ==, ALL_VISIBLE);
}

static void test_dual_progress_bar4() {
  sdi_progress_window_end_refresh(progress_window, "D-SNAP");
  g_assert_cmpint(count_hash_childs(), ==, 0);
  g_assert_cmpint(count_progress_childs(), ==, -1);
}

/**
 * GApplication callbacks
 */

static void do_startup(GObject *object, gpointer data) {
  progress_window = sdi_progress_window_new(G_APPLICATION(object));

  g_assert_nonnull(app_list);
}

static void do_activate(GApplication *app, gpointer data) {
  g_application_hold(app);
  // add tests
  g_test_add_func("/progress_window/test_progress_bar", test_progress_bar);
  g_test_add_func("/progress_window/test_pulsing_bar", test_pulse_bar);
  g_test_add_func("/progress_window/test_close_bar", test_close_bar);
  g_test_add_func("/progress_window/test_manual_hide", test_manual_hide);
  g_test_add_func("/progress_window/test_dual_progress_bar_1",
                  test_dual_progress_bar1);
  g_test_add_func("/progress_window/test_dual_progress_bar_2",
                  test_dual_progress_bar2);
  g_test_add_func("/progress_window/test_dual_progress_bar_3",
                  test_dual_progress_bar3);
  g_test_add_func("/progress_window/test_dual_progress_bar_4",
                  test_dual_progress_bar4);

  g_test_run();
  g_application_release(app);
}

static gchar *get_data_path() {
  g_autofree gchar *path = g_test_build_filename(G_TEST_BUILT, "data", NULL);
  return g_canonicalize_filename(path, NULL);
}

void set_environment() {
  g_autofree gchar *share_path = get_data_path();
  g_autofree gchar *newvar = g_strdup_printf("%s:/usr/share/", share_path);
  g_setenv("XDG_DATA_DIRS", newvar, TRUE);
  g_setenv("GTK_A11Y", "none", 1);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  set_environment();

  g_autoptr(GApplication) app = G_APPLICATION(gtk_application_new(
      "io.snapcraft.SdiProgressDockTest", G_APPLICATION_DEFAULT_FLAGS));
  g_signal_connect(app, "startup", (GCallback)do_startup, NULL);
  g_signal_connect(app, "activate", (GCallback)do_activate, NULL);
  g_application_run(app, argc, argv);
  g_print("SDI progress window test done\n");
}
