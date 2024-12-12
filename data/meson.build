subdir('resources')

gnome = import('gnome')

# The returned source would be passed to another target
login_src = gnome.gdbus_codegen('org.freedesktop.login1',
  sources: 'org.freedesktop.login1.dbus.xml',
  interface_prefix : 'org.freedesktop'
)

login_session_src = gnome.gdbus_codegen('org.freedesktop.login1.Session',
  sources: 'org.freedesktop.login1.session.dbus.xml',
  interface_prefix : 'org.freedesktop.login1.session'
)

unity_launcher_src = gnome.gdbus_codegen('com.canonical.Unity.LauncherEntry',
  sources: 'com.canonical.Unity.LauncherEntry.dbus.xml',
  interface_prefix : 'com.canonical.unity.launcherentry',
  namespace: 'Unity'
)

desktop_launcher_src = gnome.gdbus_codegen('io.snapcraft.PrivilegedDesktopLauncher',
  sources: 'io.snapcraft.PrivilegedDesktopLauncher.dbus.xml',
  interface_prefix : 'io.snapcraft.PrivilegedDesktopLauncher',
  namespace: 'PrivilegedDesktopLauncher'
)

if (DO_INSTALL)
  install_data('io.snapcraft.SnapDesktopIntegration.desktop', install_dir: 'share/applications')
  install_data('snapd-desktop-integration.svg', install_dir: 'share/icons/hicolor/scalable/apps')
endif
