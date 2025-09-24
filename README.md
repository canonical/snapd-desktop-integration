# snapd-desktop-integration
User session helpers for snapd

## Code formatting

Use "./format-source.sh" after any change to automatically format the code.

You can also add that script to your `.git/hooks/pre-commit` script to make
an automatic format before any commit (also add `export SOURCE_PATHS="src tests"`
before to only format the folders with sources). Or add it with the `pre-commit`
parameter to just check the format without modifying it.

## Tests and coverage check

There are several unitary tests for the code, although one of them can't be
run automatically, and require the user/developer to run it. It is:

- tests/test-sdi-notices-progress-window

It requires the developer to manually click on several windows.

To run the test, use the `run-test-sdi-progress-window` script located at
the root folder. This script will automagically compile the source code with
coverage support, launch the test, and update the coverage XML file.

When a commit is sent to GitHub, the CI runs the automatic unitary tests,
generates the corresponding coverage file, and merges it with the one generated
here to upload the aggregated results as an artifact with the name
`tics-coverage-report`. If any of the files needed for a test has been modified
and no new test has been run, the CI will notice it and show an error.

To compile the code with coverage check, you must pass *-Dadd-coverage* option
to Meson. For security reasons, enabling it will disable the *install* option,
to avoid installing system-wide binaries with coverage code inside.
