# @file    fw.mk
# @brief   The compile-in firmware blobs: one generator, run before the sources
#          are named.
#
# Every wireless line compiles its firmware in as a C array, because the image
# has no filesystem.  The arrays are generated from the vendor images tracked
# in `components/liblinux80211/fw/` (a clean clone must not need the GT tree)
# by `tools/rtos/fw-blobs.py`, whose table names each source file, request
# name, C symbol and output unit.
#
# Before this fragment each driver's sources.mk carried its own `$(error)` when
# its blob was missing, so a clean checkout failed at parse time one line at a
# time and the round trip was "run the generator by hand, retry the build".  A
# blob is a build product of a file in the tree, so the build makes it, the same
# way it makes the generated Kconfig fragment.
#
# It has to run while the makefiles are being read, not as a rule: the blob
# paths are words in the source lists that become targets, and make cannot grow
# a prerequisite list after it has read every makefile.  The generator is a
# no-op when the blobs are current (it compares the emitted text), so this is
# one Python start per image, and a blob whose bytes did change gets a new mtime
# - which is what the object rule already keys on.

FW_TOOL := $(ROOT)/tools/rtos/fw-blobs.py

# The `|| echo` is what carries the exit status into make: a failed run that
# left a stale blob behind would otherwise pass the wildcard guard below.
FW_GEN := $(shell $(PYTHON) $(FW_TOOL) --all 2>&1 || echo fw-blobs: FAIL)
ifneq ($(findstring fw-blobs: FAIL,$(FW_GEN)),)
$(error fw: the firmware blobs could not be generated: $(FW_GEN))
endif
# The tool's table is the authority for which units exist; this list only has
# to notice that every one of them was produced.  A new blob is one more word
# here and one more row in tools/rtos/fw-blobs.py.
FW_BLOBS := \
	$(ROOT)/components/liblinux80211/fw/iwl7260_fw_blob.c \
	$(ROOT)/components/liblinux80211/fw/rtw8851b_fw_blob.c
FW_MISSING := $(foreach f,$(FW_BLOBS),$(if $(wildcard $(f)),,$(f)))
ifneq ($(strip $(FW_MISSING)),)
$(error fw: $(FW_TOOL) --all did not produce $(FW_MISSING); run it by hand to see why)
endif
