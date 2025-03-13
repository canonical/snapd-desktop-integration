conf = configuration_data()
conf.set_quoted('DAEMON_BUILDDIR', daemon_builddir)
configure_file(output: 'config.h',
               configuration: conf)

test_executable = executable(
  'test-snapd-desktop-integration',
  'test-snapd-desktop-integration.c',
  dependencies: [gio_dep, gio_unix_dep, json_glib_dep, libsoup_dep],
  c_args: COVERAGE_C_ARGS,
  link_args: COVERAGE_LINK_ARGS,
  install: false,
)

test('Tests', test_executable)

sdi_notify_executable = executable(
  'test-sdi-notify',
  'test-sdi-notify.c',
  'mock-fdo-notifications.c',
  '../src/sdi-notify.c',
  '../src/sdi-helpers.c',
  desktop_launcher_src,
  dependencies: [gtk_dep, snapd_glib_dep, gio_dep, libnotify_dep],
  c_args: ['-DDEBUG_TESTS'] + COVERAGE_C_ARGS,
  link_args: COVERAGE_LINK_ARGS,
  install: false,
)

test_sdi_notices_monitor = executable(
  'test-sdi-notices-monitor',
  'test-sdi-notices-monitor.c',
  'mock-snapd.c',
  '../src/sdi-snapd-monitor.c',
  '../src/sdi-snapd-client-factory.c',
  dependencies: [gtk_dep, snapd_glib_dep, gio_dep, libsoup_dep, json_glib_dep],
  c_args: ['-DDEBUG_TESTS'] + COVERAGE_C_ARGS,
  link_args: COVERAGE_LINK_ARGS,
  install: false,
)

test_sdi_progress_dock_executable = executable(
  'test-sdi-progress-dock',
  'test-sdi-progress-dock.c',
  '../src/sdi-progress-dock.c',
  unity_launcher_src,
  dependencies: [gtk_dep, snapd_glib_dep, gio_dep],
  c_args: ['-DDEBUG_TESTS'] + COVERAGE_C_ARGS,
  link_args: COVERAGE_LINK_ARGS,
  install: false,
)

test_sdi_progress_window_executable = executable(
  'test-sdi-progress-window',
  'test-sdi-progress-window.c',
  '../src/sdi-progress-window.c',
  '../src/sdi-refresh-dialog.c',
  resources,
  dependencies: [gtk_dep, snapd_glib_dep, gio_dep],
  c_args: ['-DDEBUG_TESTS'] + COVERAGE_C_ARGS,
  link_args: COVERAGE_LINK_ARGS,
  install: false,
)

subdir('data')

test('Tests', test_executable)
