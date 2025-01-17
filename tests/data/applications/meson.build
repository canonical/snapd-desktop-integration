fs = import('fs')

fs.copyfile('kicad_kicad.desktop', 'kicad_kicad.desktop')
fs.copyfile('kicad-no-icon_kicad-no-icon.desktop', 'kicad-no-icon_kicad-no-icon.desktop')
configure_file(input: 'simple-scan_simple-scan.desktop.in',
               output: 'simple-scan_simple-scan.desktop',
               configuration: {'BUILD_DIR': meson.project_build_root()})
