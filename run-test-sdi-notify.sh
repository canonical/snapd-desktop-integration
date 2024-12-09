#!/bin/bash

# This script must be run after any change to `sdi-notify.c`, to create the
# COBERTURA.XML file for that code.
# This can't be run in the CI hook because the test is interactive and
# requires an user to do certain actions and checks.

set -e

FOLDER=.build_test_notify

rm -rf ${FOLDER}
meson setup ${FOLDER}
ninja -C ${FOLDER}
./${FOLDER}/tests/test-sdi-notify
gcovr ${FOLDER} --cobertura coverage/cobertura-notify.xml
rm -rf ${FOLDER}
