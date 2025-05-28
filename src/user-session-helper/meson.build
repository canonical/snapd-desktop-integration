user_session_helper = executable(
  'user-session-helper',
  'sdi-user-session-helper.c',
  resources, dbus_server_src, login_src, login_session_src, unity_launcher_src, desktop_launcher_src,
  dependencies: [glib_dep, gobject_dep, gio_dep],
  install: DO_INSTALL,
  c_args: COVERAGE_C_ARGS,
  link_args: ['-rdynamic'] + COVERAGE_LINK_ARGS,
)
