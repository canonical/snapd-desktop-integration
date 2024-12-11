#!/bin/bash

# This script must be run after any change to `sdi-notify.c`, to create the
# COBERTURA.XML file for that code.
# This can't be run in the CI hook because the test is interactive and
# requires an user to do certain actions and checks.

set -e

FOLDER=.build_test_theme_change

rm -rf ${FOLDER}
meson setup ${FOLDER} -Dadd-coverage=true
ninja -C ${FOLDER}
echo Running the test
./${FOLDER}/tests/test-snapd-desktop-integration
gcovr ${FOLDER} --cobertura coverage/cobertura-theme-change.xml
rm -rf ${FOLDER}
