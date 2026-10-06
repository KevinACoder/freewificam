# Suite declaration: upstream ThreadX regression tests on the Linux userspace port.
#
# The container reviews this before anything is built: a truth source is mandatory, and
# this suite runs the upstream bytes as they are - the vendored tree is never patched.

SUITE_NAME := threadx-regression
SUITE_TIMEOUT := 600
SUITE_EXPECT := self
SUITE_BUILD := tools/mock/threadx-regression/build.sh
SUITE_RUN := tools/mock/threadx-regression/run-tests.sh
SUITE_TRUTH_SOURCE := Eclipse ThreadX upstream regression suite vendored at the pinned commit (third-party/threadx/test/tx/regression) - expectations are the upstream framework's own, not this repository's
SUITE_SEGMENTS := -
SUITE_WIRELESS := 0
