gnome = import('gnome')

# The returned source would be passed to another target
gdbus_src = gnome.gdbus_codegen('io.snapcraft.SnapDesktopIntegration',
  sources: 'io.snapcraft.SnapDesktopIntegration.dbus.xml',
  interface_prefix : 'io.snapcraft.',
  docbook : 'SnapDesktopIntegration-interface-doc'
)

conf = configuration_data()
conf.set_quoted('LOCALEDIR', get_option('prefix') / get_option('localedir'))
configure_file(output: 'config.h',
               configuration: conf)

subdir('resources')

snapd_desktop_integration = executable(
  'snapd-desktop-integration',
  'main.c', 'refresh_status.c', 'dbus.c',
  resources, gdbus_src,login_src, login_session_src,
  dependencies: [gtk_dep, snapd_glib_dep, libnotify_dep],
  install: true,
  link_args: ['-rdynamic'],
)

daemon_builddir = meson.current_build_dir()
