#!/bin/bash

# This script must be run after any change to `sdi-notify.c`, to create the
# COBERTURA.XML file for that code.
# This can't be run in the CI hook because the test is interactive and
# requires an user to do certain actions and checks.

set -e

# ensure that the source code is correctly formatted
# This is paramount, because if it's not, it would be
# re-formatted on commit, so it could change.

./format-source.sh check

FOLDER=.build_test_notify

rm -rf ${FOLDER}
meson setup ${FOLDER} -Dadd-coverage=true
ninja -C ${FOLDER}
echo Running the test
mkdir -p coverage
./${FOLDER}/tests/test-sdi-notify
gcovr ${FOLDER} --cobertura coverage_base/cobertura-notify.xml
rm -rf ${FOLDER}
