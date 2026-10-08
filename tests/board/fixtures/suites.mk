# Fixture registry: the synthetic registration the registry reader is
# exercised with, in the same make-assignment syntax as the board registry
# (tests/board/suites.mk) - one file, read by the host runner and includable
# by a Makefile aggregate.
#
# Only the valid fixture is registered. The negative declarations under neg-*
# are validated one by one (each of them must be rejected); they are never
# part of a registry, so a broken declaration can never be run by accident.
#
# @design declarative registry - ref tools/mock/suites.mk (the same
#          make-assignment syntax, instantiated for the fixture set).
SUITES_BOARD := fixture-accept
