# Suite declaration: the container's own self test.
#
# A container that cannot go red is worthless, so this suite proves both directions: the
# declared expectations are classified as declared on purpose-built fixtures, and a set of
# deliberately broken suites must make the container fail. Neither half is optional.

SUITE_NAME := container-selftest
SUITE_TIMEOUT := 120
SUITE_EXPECT := self
SUITE_BUILD := sh tools/mock/container-selftest/build.sh
SUITE_RUN := sh tools/mock/container-selftest/run.sh
SUITE_TRUTH_SOURCE := The expectation semantics the container itself promises (expected red, designed crash, compile failure, plain pass, signature match) - the fixtures are built to hit each form exactly
SUITE_SEGMENTS := -
SUITE_WIRELESS := 0
