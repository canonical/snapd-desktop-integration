snapd_desktop_integration = executable(
  'snapd-desktop-integration',
  'main.c', 'refresh_status.c', 'dbus.c',
  dependencies: [gtk_dep, snapd_glib_dep, libnotify_dep],
  install: true,
)

daemon_builddir = meson.current_build_dir()
