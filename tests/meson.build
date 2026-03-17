conf = configuration_data()
conf.set_quoted('DAEMON_BUILDDIR', daemon_builddir)
configure_file(output: 'config.h',
               configuration: conf)

wlheadless_run = find_program('wlheadless-run', required: false)
weston = find_program('weston', required: false)

test_environment = environment()
test_environment.set('GSETTINGS_BACKEND', 'memory')

add_test_setup('default',
  is_default: true,
  env: test_environment,
)

add_test_setup('headless',
  env: test_environment,
  exe_wrapper: wlheadless_run.found() and weston.found() ? [
    wlheadless_run, '-c', fs.name(weston.full_path()), '--'
  ] : [],
)

test_executable = executable(
  'test-snapd-desktop-integration',
  'test-snapd-desktop-integration.c',
  dependencies: [gio_dep, gio_unix_dep, json_glib_dep, libsoup_dep],
  c_args: COVERAGE_C_ARGS,
  link_args: COVERAGE_LINK_ARGS,
  install: false,
)
test('snapd-desktop-integration', test_executable,
  timeout: 90)

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
test('sdi-notify', sdi_notify_executable)

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
test('sdi-notices-monitor', test_sdi_notices_monitor)

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
test('sdi-progress-dock', test_sdi_progress_dock_executable)

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
test('sdi-progress-window', test_sdi_progress_window_executable,
  timeout: 90)

test_refresh_monitor = executable(
  'test-refresh-monitor',
  'test-refresh-monitor.c',
  'mock-snapd.c',
  '../src/sdi-refresh-monitor.c',
  '../src/sdi-snap.c',
  '../src/sdi-helpers.c',
  '../src/sdi-snapd-client-factory.c',
  resources,
  dependencies: [gtk_dep, snapd_glib_dep, gio_dep, libsoup_dep, json_glib_dep],
  c_args: ['-DDEBUG_TESTS','-DSNAPS_DESKTOP_FILES_FOLDER="' + meson.source_root() + '/tests/data/applications"'] + COVERAGE_C_ARGS,
  link_args: COVERAGE_LINK_ARGS,
  install: false,
)
test('refresh-monitor', test_refresh_monitor,
  timeout: 90)

subdir('data')
