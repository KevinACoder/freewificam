# tools/insight - diagnostic and forensic commands

This directory is the single home for the project's diagnostic and forensic
commands: the console commands and host tools that inspect a running image or a
captured trace rather than exercise a feature.

## Convention

**New diagnostic and forensic capabilities are added here, not scattered
across the test trees.** A command that answers "what is the device doing
right now" or "what did the last failure look like" belongs in this directory,
registered through the same command-export mechanism the shell uses.

The reason is discoverability: when a board run wedges, the first question is
"what instrumentation already exists", and the answer must be one directory,
not a grep across the tree.

## Current state

The convention is set here; the migration is not finished. Diagnostic commands
written before this directory existed still live next to the suites that use
them, in the board test trees (`tests/board/threadx-rk3568-*/tests/*_cmds.c`
and the ledger files beside them). They are the reference for the shape a
command here should take; new ones land in this directory.

Each capability should state, in its own header:

- what it observes, and at which boundary (device side vs. consumer side);
- whether it is always on or behind a runtime switch;
- whether it can perturb the thing it measures.
