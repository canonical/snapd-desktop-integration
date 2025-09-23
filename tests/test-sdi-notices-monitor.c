#include "../src/sdi-snapd-client-factory.h"
#include "../src/sdi-snapd-monitor.h"
#include "mock-snapd.h"

typedef struct {
  GMainLoop *loop;
  MockSnapd *snapd;
  int counter;
} AsyncData;

static AsyncData *async_data_new(GMainLoop *loop, MockSnapd *snapd) {
  AsyncData *data = g_slice_new0(AsyncData);
  data->loop = g_main_loop_ref(loop);
  data->snapd = g_object_ref(snapd);
  return data;
}

static void async_data_free(AsyncData *data) {
  g_main_loop_unref(data->loop);
  g_object_unref(data->snapd);
  g_slice_free(AsyncData, data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(AsyncData, async_data_free)

static MockNotice *create_notice(MockSnapd *snapd, const gchar *kind) {
  MockNotice *notice = mock_snapd_add_notice(snapd, "1", "8473", kind);
  g_autoptr(GTimeZone) timezone = g_time_zone_new_utc();

  g_autoptr(GDateTime) first_occurred =
      g_date_time_new(timezone, 2024, 3, 1, 20, 29, 58);
  g_autoptr(GDateTime) last_occurred =
      g_date_time_new(timezone, 2024, 3, 2, 23, 28, 8);
  g_autoptr(GDateTime) last_repeated =
      g_date_time_new(timezone, 2024, 3, 3, 22, 20, 7);
  mock_notice_set_dates(notice, first_occurred, last_occurred, last_repeated,
                        5);
  mock_notice_set_nanoseconds(notice, 6);
  return notice;
}

static void test_notices_events_are_received_cb(SdiSnapdMonitor *self,
                                                SnapdNotice *notice,
                                                gboolean first_set,
                                                AsyncData *data) {
  data->counter++;
  switch (data->counter) {
  case 1:
    g_assert_true(first_set);
    g_assert_cmpint(snapd_notice_get_notice_type(notice), ==,
                    SNAPD_NOTICE_TYPE_CHANGE_UPDATE);
    /* close the socket
     * Called twice because there are two references:
     * one in the 'snapd' variable, in the calling function, and another one in
     * `data` struct.
     */
    g_object_unref(data->snapd);
    g_object_unref(data->snapd);
    /* we create a new snapd mock, simulating that the daemon died and a new one
     * was launched
     */
    data->snapd = g_object_ref(mock_snapd_new());
    const gchar *path = mock_snapd_get_socket_path(data->snapd);
    g_assert_true(mock_snapd_start(data->snapd, NULL));
    sdi_snapd_client_factory_set_custom_path((gchar *)path);
    create_notice(data->snapd, "refresh-inhibit");
    break;
  case 2:
    g_assert_true(first_set);
    g_assert_cmpint(snapd_notice_get_notice_type(notice), ==,
                    SNAPD_NOTICE_TYPE_REFRESH_INHIBIT);
    g_main_loop_quit(data->loop);
    break;
  }
}

static void test_notices_events_are_received(void) {
  g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);

  MockSnapd *snapd = mock_snapd_new();
  g_autoptr(AsyncData) data = async_data_new(loop, snapd);

  const gchar *path = mock_snapd_get_socket_path(snapd);
  sdi_snapd_client_factory_set_custom_path((gchar *)path);
  g_autoptr(GError) error = NULL;
  g_assert_true(mock_snapd_start(snapd, &error));

  g_autoptr(SdiSnapdMonitor) snapd_monitor = sdi_snapd_monitor_new();
  g_signal_connect(G_OBJECT(snapd_monitor), "notice-event",
                   G_CALLBACK(test_notices_events_are_received_cb), data);

  create_notice(snapd, "change-update");

  g_assert_true(sdi_snapd_monitor_start(snapd_monitor));
  g_main_loop_run(loop);
  g_assert_cmpint(data->counter, ==, 2);
  g_object_unref(data->snapd); // it has two references
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/sdi-snapd-monitor/receive-notices",
                  test_notices_events_are_received);
  return g_test_run();
}
