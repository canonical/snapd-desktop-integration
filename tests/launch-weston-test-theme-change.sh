#!/bin/sh

./_build/tests/test-snapd-desktop-integration
if [ $? -ne 0 ]; then
    # sending a kill -9 makes weston return a non-zero value, thus
    # notifying CI/CD pipeline that this test failed.
    killall -s 9 weston
fi
