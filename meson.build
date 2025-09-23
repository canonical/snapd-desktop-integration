project('snapd-desktop-integration', 'c', version: '0.9', meson_version: '>= 0.64.0')

if get_option('gnotify')
    add_global_arguments('-DUSE_GNOTIFY', language: 'c')
endif

gio_dep = dependency('gio-2.0')
gio_unix_dep = dependency('gio-unix-2.0')
gtk_dep = dependency('gtk4', version: '>= 4.0')
snapd_glib_dep = dependency('snapd-glib-2', required: false, version: '>= 1.64')
if not snapd_glib_dep.found()
    snapd_glib_dep = dependency('snapd-glib', version: '>= 1.60')
endif
libnotify_dep = dependency('libnotify', version: '>= 0.7.7')
libsoup_dep = dependency ('libsoup-3.0')
json_glib_dep = dependency ('json-glib-1.0')

add_global_arguments('-DGETTEXT_PACKAGE="@0@"'.format (meson.project_name()), language:'c')

if get_option('add-coverage')
    COVERAGE_C_ARGS = ['-fprofile-arcs', '-ftest-coverage', '-fPIC', '-O0']
    COVERAGE_LINK_ARGS = ['-lgcov', '--coverage']
    # don't allow to do install if coverage is enabled
    DO_INSTALL = false
else
    COVERAGE_C_ARGS = []
    COVERAGE_LINK_ARGS = []
    DO_INSTALL = true
endif

subdir('data')
subdir('po')
subdir('src')
subdir('tests')

if (not DO_INSTALL)
    message('\x1B[0;31mCoverage option detected. Install is disabled\x1B[0m')
endif
