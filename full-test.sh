#!/bin/bash
# This script can be copied into your base directory for use with
# automated testing using assignment-autotest.  It automates the
# steps described in https://github.com/cu-ecen-5013/assignment-autotest/blob/master/README.md#running-tests
set -e

cd `dirname $0`
test_dir=`pwd`
echo "starting test with SKIP_BUILD=\"${SKIP_BUILD}\" and DO_VALIDATE=\"${DO_VALIDATE}\""

# This part of the script always runs as the current user, even when
# executed inside a docker container.
# See the logic in parse_docker_options for implementation
logfile=test.sh.log
# See https://stackoverflow.com/a/3403786
# Place stdout and stderr in a log file
exec > >(tee -i -a "$logfile") 2> >(tee -i -a "$logfile" >&2)

echo "Running test with user $(whoami)"

set +e


# Run unit tests if present
if [ -x "./unit-test.sh" ]; then
    ./unit-test.sh
    unit_test_rc=$?
    if [ $unit_test_rc -ne 0 ]; then
        echo "Unit test failed"
    fi
else
    echo "No unit tests found, skipping"
    unit_test_rc=0
fi

# If there's a configuration for the assignment number, use this to look for
# additional tests
ASSIGNMENT_FILE="$test_dir/conf/assignment.txt"
if [ ! -f "$ASSIGNMENT_FILE" ]; then
    echo "Missing conf/assignment.txt, no assignment to run"
    exit 1
fi

assignment="$(cat "$ASSIGNMENT_FILE")"
ASSIGNMENT_TEST="$test_dir/assignment-autotest/test/${assignment}/assignment-test.sh"

if [ ! -x "$ASSIGNMENT_TEST" ]; then
    echo "ERROR: assignment-test.sh not found at:"
    echo "  $ASSIGNMENT_TEST"
    exit 1
fi

# Run the assignment test
"$ASSIGNMENT_TEST" "$test_dir"
assign_rc=$?
if [ $assign_rc -eq 0 ]; then
    echo "Assignment test ${assignment} complete with success"
else
    echo "Assignment test ${assignment} failed with rc=${assign_rc}"
    exit $assign_rc
fi

exit $unit_test_rc

