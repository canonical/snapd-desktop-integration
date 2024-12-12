# snapd-desktop-integration
User session helpers for snapd

## Code formatting

Use "clang-format -i SOURCE_FILE_NAME" after any change to automatically
format the code.

## Tests and coverage check

There are several unitary tests for the code, although some of them can't be
run automatically, and require the user/developer to run them. These are:

- tests/test-sdi-notices-monitor
- tests/test-snapd-desktop-integration

The first one requires the developer to manually click on several notifications.

The second one has some requirements that can't be fulfilled in the CI containers.

To run each test, use the `run-test-XXXX` scripts located at the root folder.
These scripts will automagically compile the source code with coverage support,
launch the test, and update the coverage XML file.

When a commit is sent to GitHub, the CI runs the automatic unitary tests,
generates the corresponding coverage file, and merges it with the ones generated
here to upload the aggregated results as an artifact with the name
`tics-coverage-report`. If any of the files needed for a test has been modified
and no new test has been run, the CI will notice it and show an error.

To compile the code with coverage check, you must pass *-Dadd-coverage* option
to Meson. For security reasons, enabling it will disable the *install* option,
to avoid installing system-wide binaries with coverage code inside.
