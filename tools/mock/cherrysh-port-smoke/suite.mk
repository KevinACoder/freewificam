# Suite declaration: the CherrySH console shell on both CMSIS-RTOS2 host runtimes.
#
# The two smoke binaries are built by their own Makefile under tests/host/ -
# the container must not grow a second copy. The suite drives one session per
# backend over a fixed input vector and reports five cases per backend
# (prompt, help, completion, notfound, exit).

SUITE_NAME := cherrysh-port-smoke
SUITE_TIMEOUT := 60
SUITE_EXPECT := self
SUITE_BUILD := make -C tests/host/cherrysh-port-smoke
SUITE_RUN := sh tools/mock/cherrysh-port-smoke/check.sh
SUITE_TRUTH_SOURCE := Upstream CherrySH sources: the not-found text and the prompt construction are upstream literals and the help listing is computed by the vendored help command from the command and variable tables themselves
SUITE_SEGMENTS := -
SUITE_WIRELESS := 0
