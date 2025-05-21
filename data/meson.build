subdir('resources')

gnome = import('gnome')

# The returned source would be passed to another target
graphical_session_target_src = gnome.gdbus_codegen('org.freedesktop.systemd1.Unit',
  sources: 'org.freedesktop.systemd1.unit.dbus.xml',
  interface_prefix : 'org.freedesktop.systemd1.Unit',
  namespace: 'GraphicalSessionTarget'
)

systemd_manager_src = gnome.gdbus_codegen('org.freedesktop.systemd1.Manager',
  sources: 'org.freedesktop.systemd1.dbus.xml',
  interface_prefix : 'org.freedesktop.systemd1.Manager',
  namespace: 'Systemd1Manager'
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
