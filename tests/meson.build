conf = configuration_data()
conf.set_quoted('DAEMON_BUILDDIR', daemon_builddir)
configure_file(output: 'config.h',
               configuration: conf)

test_executable = executable(
  'test-snapd-desktop-integration',
  'test-snapd-desktop-integration.c',
  dependencies: [gio_dep, gio_unix_dep, json_glib_dep, libsoup_dep],
)

test_sdi_progress_dock_executable = executable(
  'test-sdi-progress-dock',
  'test-sdi-progress-dock.c',
  '../src/sdi-progress-dock.c',
  unity_launcher_src,
  dependencies: [gtk_dep, snapd_glib_dep, gio_dep],
  c_args: ['-DDEBUG_TESTS']
)

test_sdi_progress_window_executable = executable(
  'test-sdi-progress-window',
  'test-sdi-progress-window.c',
  '../src/sdi-progress-window.c',
  '../src/sdi-refresh-dialog.c',
  resources,
  dependencies: [gtk_dep, snapd_glib_dep, gio_dep],
  c_args: ['-DDEBUG_TESTS']
)
