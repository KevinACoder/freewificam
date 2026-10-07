# Gate1 container self test fixtures
#
# This directory holds the source of the compiled fixtures only (a pass, a designed red,
# a designed crash, and the negative source that must never compile). The vector
# negatives half generates its broken mini vector trees at run time, in a temporary
# directory, so no deliberately broken vector ever lands in the factory's data set.
