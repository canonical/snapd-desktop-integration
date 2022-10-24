conf = configuration_data()
conf.set_quoted('LOCALEDIR', get_option('prefix') / get_option('localedir'))
configure_file(output: 'config.h',
               configuration: conf)

subdir('resources')

snapd_desktop_integration = executable(
  'snapd-desktop-integration',
  'main.c', 'refresh_status.c', 'dbus.c', resources,
  dependencies: [gtk_dep, snapd_glib_dep, libnotify_dep],
  install: true,
  link_args: ['-rdynamic'],
)

daemon_builddir = meson.current_build_dir()
