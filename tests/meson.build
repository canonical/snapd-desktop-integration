conf = configuration_data()
conf.set_quoted('DAEMON_BUILDDIR', daemon_builddir)
configure_file(output: 'config.h',
               configuration: conf)

test_executable = executable(
  'test-snapd-desktop-integration',
  'test-snapd-desktop-integration.c',
  dependencies: [gio_dep, gio_unix_dep, json_glib_dep, libsoup_dep],
)

test('Tests', test_executable)
