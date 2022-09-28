snapd_desktop_integration = executable(
  'snapd-desktop-integration',
  'main.c', 'ds_state.c', 'themes.c',
  dependencies: [gtk_dep, snapd_glib_dep, libnotify_dep],
  install: true,
)

daemon_builddir = meson.current_build_dir()
