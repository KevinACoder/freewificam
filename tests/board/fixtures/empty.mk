# The empty-set declaration: a registry with nothing registered. The runner
# states the empty set instead of leaving it open (the same discipline the
# host-side container follows for its wireless set), which is how "no board
# suite is registered yet" stays a fact of the file rather than an inference.
#
# @design declarative registry, empty set - ref tools/mock/suites.mk (the same
#          file shape with nothing registered).
SUITES_BOARD :=
