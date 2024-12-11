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

test_sdi_empty = executable(
  'test-sdi-empty',
  'test-sdi-empty.c',
  'mock-snapd.c',
  '../src/sdi-refresh-dialog.c',
  '../src/sdi-refresh-monitor.c',
  '../src/sdi-snap.c',
  '../src/sdi-snapd-monitor.c',
  '../src/sdi-snapd-client-factory.c',
  '../src/sdi-helpers.c',
  resources, desktop_launcher_src, unity_launcher_src,
  dependencies: [gtk_dep, snapd_glib_dep, gio_dep, libnotify_dep, libsoup_dep, json_glib_dep],
  c_args: ['-DDEBUG_TESTS'] + COVERAGE_C_ARGS,
  link_args: COVERAGE_LINK_ARGS,
  install: false,
)

subdir('data')
