# @file    wl80211.mk
# @brief   The wireless world: the imported mac80211/cfg80211 stack and, when
#          the profile asks for it, the vendored iwlwifi (AC 7260 / mvm) driver.
#
# This is a flag world, not a second component rule: the stack set is the one
# components/liblinux80211/sources.mk names, the driver set the one
# wireless-drivers/iwlwifi/sources.mk names.  The platform Makefile compiles
# them here because each needs its own Linux face:
#
#   stack   S0's face: the component's include roots + stack_config.h, the
#           three diagnostics that make a missing API a hard error
#   driver  the same face plus the driver's root and driver_config.h, and the
#           kernel build's warning relaxations (the driver is kernel code)
#   case    the stack face; a board case includes <net/mac80211.h> and calls
#           the public entry points only
#
# The stack must be selected by CONFIG_LIBLINUX80211_WIRELESS; the driver adds
# CONFIG_LIBLINUX80211_IWLWIFI.  A profile that selects neither links none of
# this (the component's kpi/bus images are unaffected).
#
# Link order is part of the contract: the stack's wireless units come before
# its mac80211 units (cfg80211's fs_initcall then mac80211's subsys_initcall),
# and inside the driver iwl-drv.c comes before mvm/ops.c (core then opmode).
# The `.lkpi_init` walk runs the section in link order; this list is where that
# order is decided.

# The board cases that compile in this world.  The platform Makefile's generic
# case list excludes them; this file owns them (defined before the include so
# both lists see the same answer).
WL80211_CASES ?= mac80211_c2 iwl7260_c3 iwl7260_c4 iwl7260_c5 iwl7260_c6 iwl7260_stab

ifeq ($(CONFIG_LIBLINUX80211_WIRELESS),1)

WL80211_STACK_CFLAGS := $(S0_CFLAGS)
WL80211_DRIVER_CFLAGS := $(S1_CFLAGS)
WL80211_CASE_CFLAGS := $(S0_CFLAGS)

# cfg80211 before mac80211: the first owns the wiphy/netdev registration the
# second calls into, and its fs_initcall entry must run before the late
# regulatory-db loader.
WL80211_STACK_SRCS := \
	$(filter $(LIB8)/stack/wireless/%,$(LIBLINUX80211_STACK_SRCS)) \
	$(filter $(LIB8)/stack/mac80211/%,$(LIBLINUX80211_STACK_SRCS))

# The compiled-in firmware unit is generated from the pinned GT blob by
# tools/rtos/fw-blobs.py; it is a build product (git-ignored) and joins the
# driver's flag world because it includes the registry header.
WL80211_FW_SRCS := $(LIB8)/fw/iwl7260_fw_blob.c

ifeq ($(CONFIG_LIBLINUX80211_IWLWIFI),1)
WL80211_DRIVER_SRCS := $(IWLWIFI_IMPL_SRCS) $(WL80211_FW_SRCS)
else
WL80211_DRIVER_SRCS :=
endif

WL80211_CASE_SRCS := $(foreach c,$(filter $(WL80211_CASES),$(CASES)),$(LINE)/app/cases/$(c).c)

# The C4/C5 bench seam, not a stack TU: it builds the real attribute bytes and
# dispatches nl80211's own handlers.  It compiles in the stack flag world but is
# not part of the S0/S1b source lists.  tx_dequeue_context.c is the same kind of
# port shim: a link-time --wrap around the stack's transmit dequeue, stating the
# port's deferred-context answer (see its header).
WL80211_PROBE_SRCS := $(LIB8)/linuxkpi_nl80211_probe.c \
	$(LIB8)/tx_dequeue_context.c

# The wrap is what makes the shim a seam instead of a stack edit.  It is only
# meaningful when the stack is linked, and it applies to every reference
# (iwlwifi's iTXQ wake path is the one that matters).
WL80211_LDFLAGS := -Wl,--wrap=ieee80211_tx_dequeue

WL80211_ALL_SRCS := $(WL80211_STACK_SRCS) $(WL80211_DRIVER_SRCS) \
	$(WL80211_CASE_SRCS) $(WL80211_PROBE_SRCS)

endif

