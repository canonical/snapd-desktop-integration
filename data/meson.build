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
