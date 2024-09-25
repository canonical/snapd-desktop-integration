conf = configuration_data()
conf.set_quoted('LOCALEDIR', get_option('prefix') / get_option('localedir'))
configure_file(output: 'config.h',
               configuration: conf)

snapd_desktop_integration = executable(
  'snapd-desktop-integration',
  'main.c',
  'sdi-notify.c',
  'sdi-snap.c',
  'sdi-refresh-dialog.c',
  'sdi-refresh-monitor.c',
  'sdi-progress-dock.c',
  'sdi-progress-window.c',
  'sdi-theme-monitor.c',
  'sdi-user-session-helper.c',
  'sdi-helpers.c',
  'sdi-snapd-monitor.c',
  'sdi-snapd-client-factory.c',
  resources, login_src, login_session_src, unity_launcher_src, desktop_launcher_src,
  dependencies: [gtk_dep, snapd_glib_dep, libnotify_dep],
  install: DO_INSTALL,
  c_args: COVERAGE_C_ARGS,
  link_args: ['-rdynamic'] + COVERAGE_LINK_ARGS,
)

daemon_builddir = meson.current_build_dir()
