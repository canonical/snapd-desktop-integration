#!/bin/bash

# The SOURCE_PATHS environment variable is an optional ENV where a
# space-separated list of paths can be configured, where the script
# will search for header and source files. If not defined, the script
# will search recursively in all the project folders

if [ $# -eq 0 ]; then
    FORMAT_CODE=1
else
    FORMAT_CODE=0
    if [[ "$1" == "pre-commit" ]]; then
        echo Checking source style
        PRE_COMMIT=1
    else
        PRE_COMMIT=0
    fi
fi

check_source_file() {
    if [ $FORMAT_CODE -eq 1 ]; then
        # no parameters? Just apply the changes
        echo Formating "$1" >&2
        clang-format -i "$1" >&2
    else
        # any parameter? check that the formatting is fine
        clang-format "$1" > "$1.formatted"
        echo Checking $1 >&2
        if [ $PRE_COMMIT -eq 0 ]; then
            if ! diff "$1" "$1.formatted" >&2; then
                echo -n false
            fi
        else
            if ! diff "$1" "$1.formatted" > /dev/null; then
                echo -n false
            fi
        fi
        rm "$1.formatted"
    fi
}

RESULT=""

if [ -n "${SOURCE_PATHS}" ]; then
# if SOURCE_PATHS is defined, search only in the specified paths
    for source_path in ${SOURCE_PATHS}; do
        RESULT=${RESULT}$(find $source_path -name '*.c' -or -name '*.h' | while read file; do
            check_source_file "$file"
        done)
    done
else

# a trick to ensure that names/folders with blank spaces do work as expected
# also, since find and while read... are executed in a subshell, they can't
# modify any variable outside. That's why we capture the output in $RESULT
# and compare it with an empty string.
# Of course, for this to work, it is mandatory to send any visible message to
# STDERR. That's why the check_source_file() function has so many >&2: those
# are commands whose output we want in the screen. This trick is needed because
# there seems to not be an easy way of storing STDERR instead of SDTOUT
    RESULT=${RESULT}$(find -name '*.c' -or -name '*.h' | while read file; do
        check_source_file "$file"
    done)
fi

if [ ! "$RESULT" = "" ]; then
    echo Failed to pass clang-format check
    exit 1
fi
