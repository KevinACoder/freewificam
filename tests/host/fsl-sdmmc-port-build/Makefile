# Static build gates for the imported SD/MMC stack, this project's port and the
# board controller drivers behind it.
#
# Self contained; run it from anywhere with:
#   make -C <repo>/tests/host/fsl-sdmmc-port-build
#
# The line has three kinds of bytes and the checks split along them:
#
#   compile    the imported protocol layer, this project's port files and the
#              board controller driver all compile for aarch64, the vendored
#              units at their own build's warning level and this project's files
#              at the stricter one
#   contract   the imported stack's shadow headers, the SDIO seam and the board
#              driver's interface compile as one translation unit on their own,
#              so the faces cannot hide behind the bytes that consume them
#   spdx       the license split is mechanical: the imported files carry exactly
#              one SPDX form (BSD-3-Clause, NXP's and Phytium's copyrights), this
#              project's files carry the project's own, and no license text has
#              been copied between them
#   gates      the kernel-isolation gates over the objects and the author-side
#              sources, with their canary proofs
#
# Warning level, and why it is what it is: the imported units are compiled with
# -Wall, the level the SDK tree itself builds them at. This project's files are
# held to -Wall -Wextra, and a warning in any file is a failure - there is no
# allowlist, because none was needed when this gate was written. If a warning
# ever appears in the imported bytes it should be recorded here with its file
# and line, the way the driver port-build suites do it, rather than silenced.

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT  := $(abspath $(MAKEFILE_DIR)/../../..)
VEND  := $(ROOT)/components/fsl_sdmmc
PORT  := $(ROOT)/ports/fsl_sdmmc
SEAM  := $(ROOT)/components/linuxkpi/sdio
BOARD := $(ROOT)/board/rk3568-itx/drivers
LKPI  := $(ROOT)/components/linuxkpi
BUILD := $(MAKEFILE_DIR)/build

AARCH64_CROSS_PATH ?= $(HOME)/workspace/env/src/xpack-aarch64-none-elf-gcc-13.2.1-1.1
CROSS_COMPILE ?= $(AARCH64_CROSS_PATH)/bin/aarch64-none-elf-
CC  := $(CROSS_COMPILE)gcc
NM  := $(CROSS_COMPILE)nm

# The imported protocol layer: the two units the SDIO build set compiles (the
# card-independent command helpers and the SDIO card driver itself), plus the MMC
# card protocol layer the storage line compiles. The card-independent common unit
# is compiled once: the card-type key does not reach it.
VENDOR_SRCS := $(VEND)/common/fsl_sdmmc_common.c $(VEND)/sdio/fsl_sdio.c

# This project's side: the OS abstraction over the facade, the two card services,
# the two host glues, the seam implementation, the seam's own dispatchers, and
# the two board controller drivers. Each image links exactly one card type's
# half, which is why both halves are gated here rather than one replacing the
# other.
PORT_SRCS := \
	$(PORT)/fsl_sdmmc_osa.c \
	$(PORT)/sdmmc_card.c \
	$(PORT)/sdmmc_host_dwmmc.c \
	$(PORT)/lkpi_sdio_fsl.c \
	$(SEAM)/lkpi_sdio_be.c \
	$(BOARD)/dwc_mmc.c

MMC_VENDOR_SRCS := $(VEND)/mmc/fsl_mmc.c

MMC_PORT_SRCS := \
	$(PORT)/sdmmc_mmc_card.c \
	$(PORT)/sdmmc_host_dwcmshc.c \
	$(BOARD)/dwc_mshc.c

SRCS := $(VENDOR_SRCS) $(PORT_SRCS) $(MMC_VENDOR_SRCS) $(MMC_PORT_SRCS)

ARCH   := -mcpu=cortex-a55 -mgeneral-regs-only -mstrict-align -mno-outline-atomics
COMMON := -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -g3 -O2 \
    -ffunction-sections -fdata-sections
INC := \
    -I$(ROOT) \
    -I$(VEND)/common -I$(VEND)/sdio -I$(VEND)/osa \
    -I$(PORT) -I$(PORT)/shadow \
    -I$(SEAM) -I$(BOARD) \
    -I$(LKPI) -I$(LKPI)/include \
    -I$(ROOT)/os/cmsis2 -I$(ROOT)/os/baremetal -I$(ROOT)/arch/aarch64 \
    -I$(ROOT)/components/ulog/src \
    -I$(ROOT)/third-party/CMSIS_6/CMSIS/RTOS2/Include
KEYS := -DCMSIS2_BACKEND_THREADX -DULOG_ENABLED -DULOG_MAX_MESSAGE_LENGTH=256 \
    -DCONFIG_FSL_SDMMC_ENABLE_SDIO=1 -DLKPI_ABS_PLATFORM_PROVIDED \
    -DCMSIS_device_header='"cmsis_os2.h"'

# The eMMC line's units compile behind the MMC card-type key instead, and with
# the MMC card layer's own include root.
MMC_KEYS := -DCMSIS2_BACKEND_THREADX -DULOG_ENABLED -DULOG_MAX_MESSAGE_LENGTH=256 \
    -DCONFIG_FSL_SDMMC_ENABLE_MMC=1 \
    -DCMSIS_device_header='"cmsis_os2.h"'

MMC_INC := $(INC) -I$(VEND)/mmc
# Header dependencies are tracked: this line's headers are edited far more often
# than the imported bytes, and a stale object would report the previous header's
# warnings.
DEPFLAGS := -MMD -MP

VENDOR_CFLAGS := $(COMMON) -Wall
PORT_CFLAGS   := $(COMMON) -Wall -Wextra

.PHONY: all compile contract spdx gates clean

all: compile contract spdx gates

# ---- compile: every unit of the line, aarch64 --------------------------------

OBJS := $(foreach s,$(SRCS),$(BUILD)/$(notdir $(basename $(s))).o)

compile: $(OBJS)
	@set -e; \
	logs=$$(cat $(OBJS:.o=.compile.log) 2>/dev/null || true); \
	warn=$$(printf '%s\n' "$$logs" | grep -c 'warning:' || true); \
	echo "compile: $(words $(SRCS)) translation units, aarch64, warning lines: $$warn"; \
	if [ "$$warn" != "0" ]; then \
		echo "FAIL: a warning in the line:"; \
		printf '%s\n' "$$logs" | grep 'warning:'; \
		exit 1; \
	fi

$(BUILD)/%.o: $(VEND)/common/%.c | $(BUILD)
	@set -e; \
	$(CC) $(ARCH) $(VENDOR_CFLAGS) $(KEYS) $(DEPFLAGS) $(INC) -c $< -o $@ 2> $(BUILD)/$*.compile.log || \
		{ cat $(BUILD)/$*.compile.log; exit 1; }

$(BUILD)/%.o: $(VEND)/sdio/%.c | $(BUILD)
	@set -e; \
	$(CC) $(ARCH) $(VENDOR_CFLAGS) $(KEYS) $(DEPFLAGS) $(INC) -c $< -o $@ 2> $(BUILD)/$*.compile.log || \
		{ cat $(BUILD)/$*.compile.log; exit 1; }

$(BUILD)/%.o: $(PORT)/%.c | $(BUILD)
	@set -e; \
	$(CC) $(ARCH) $(PORT_CFLAGS) $(KEYS) $(DEPFLAGS) $(INC) -c $< -o $@ 2> $(BUILD)/$*.compile.log || \
		{ cat $(BUILD)/$*.compile.log; exit 1; }

$(BUILD)/%.o: $(SEAM)/%.c | $(BUILD)
	@set -e; \
	$(CC) $(ARCH) $(PORT_CFLAGS) $(KEYS) $(DEPFLAGS) $(INC) -c $< -o $@ 2> $(BUILD)/$*.compile.log || \
		{ cat $(BUILD)/$*.compile.log; exit 1; }

$(BUILD)/%.o: $(BOARD)/%.c | $(BUILD)
	@set -e; \
	$(CC) $(ARCH) $(PORT_CFLAGS) $(KEYS) $(DEPFLAGS) $(INC) -c $< -o $@ 2> $(BUILD)/$*.compile.log || \
		{ cat $(BUILD)/$*.compile.log; exit 1; }

# The eMMC line's units: the MMC card protocol layer at the imported bytes'
# warning level, this project's port and the board driver at the project's, all
# behind the MMC key. Explicit rules, because the pattern rules above carry the
# SDIO key.
$(BUILD)/fsl_mmc.o: $(VEND)/mmc/fsl_mmc.c | $(BUILD)
	@set -e; \
	$(CC) $(ARCH) $(VENDOR_CFLAGS) $(MMC_KEYS) $(DEPFLAGS) $(MMC_INC) -c $< -o $@ 2> $(BUILD)/fsl_mmc.compile.log || \
		{ cat $(BUILD)/fsl_mmc.compile.log; exit 1; }

$(BUILD)/sdmmc_mmc_card.o: $(PORT)/sdmmc_mmc_card.c | $(BUILD)
	@set -e; \
	$(CC) $(ARCH) $(PORT_CFLAGS) $(MMC_KEYS) $(DEPFLAGS) $(MMC_INC) -c $< -o $@ 2> $(BUILD)/sdmmc_mmc_card.compile.log || \
		{ cat $(BUILD)/sdmmc_mmc_card.compile.log; exit 1; }

$(BUILD)/sdmmc_host_dwcmshc.o: $(PORT)/sdmmc_host_dwcmshc.c | $(BUILD)
	@set -e; \
	$(CC) $(ARCH) $(PORT_CFLAGS) $(MMC_KEYS) $(DEPFLAGS) $(MMC_INC) -c $< -o $@ 2> $(BUILD)/sdmmc_host_dwcmshc.compile.log || \
		{ cat $(BUILD)/sdmmc_host_dwcmshc.compile.log; exit 1; }

$(BUILD)/dwc_mshc.o: $(BOARD)/dwc_mshc.c | $(BUILD)
	@set -e; \
	$(CC) $(ARCH) $(PORT_CFLAGS) $(MMC_KEYS) $(DEPFLAGS) $(MMC_INC) -c $< -o $@ 2> $(BUILD)/dwc_mshc.compile.log || \
		{ cat $(BUILD)/dwc_mshc.compile.log; exit 1; }

# ---- contract: the faces on their own ----------------------------------------

contract: $(BUILD)/face_contract.o $(BUILD)/face_contract_mmc.o
	@set -e; \
	log=$$(cat $(BUILD)/face_contract.compile.log $(BUILD)/face_contract_mmc.compile.log 2>/dev/null || true); \
	warn=$$(printf '%s\n' "$$log" | grep -c 'warning:' || true); \
	echo "contract: the imported shadows, the seam and both board interfaces compile standalone, warning lines: $$warn"; \
	if [ "$$warn" != "0" ]; then \
		echo "FAIL: a face must compile without warnings of its own:"; \
		printf '%s\n' "$$log" | grep 'warning:'; \
		exit 1; \
	fi

$(BUILD)/face_contract.o: $(MAKEFILE_DIR)/face_contract.c | $(BUILD)
	@set -e; \
	$(CC) $(ARCH) $(PORT_CFLAGS) $(KEYS) $(DEPFLAGS) $(INC) -c $< -o $@ 2> $(BUILD)/face_contract.compile.log || \
		{ cat $(BUILD)/face_contract.compile.log; exit 1; }

# The eMMC line's contract unit: the same shape, behind the MMC key.
$(BUILD)/face_contract_mmc.o: $(MAKEFILE_DIR)/face_contract_mmc.c | $(BUILD)
	@set -e; \
	$(CC) $(ARCH) $(PORT_CFLAGS) $(MMC_KEYS) $(DEPFLAGS) $(MMC_INC) -c $< -o $@ 2> $(BUILD)/face_contract_mmc.compile.log || \
		{ cat $(BUILD)/face_contract_mmc.compile.log; exit 1; }

-include $(wildcard $(BUILD)/*.d)

# ---- spdx: the license split, mechanically -----------------------------------

spdx:
	@set -e; \
	vend="$$(find $(VEND) -name '*.c' -o -name '*.h')"; \
	n=$$(printf '%s\n' "$$vend" | wc -l); \
	bad=$$(grep -L 'SPDX-License-Identifier: BSD-3-Clause' $$vend || true); \
	if [ -n "$$bad" ]; then echo "FAIL: an imported file without its BSD-3 SPDX line:"; printf '%s\n' "$$bad"; exit 1; fi; \
	extra=$$(grep -h 'SPDX-License-Identifier' $$vend | sort -u | grep -v 'BSD-3-Clause' || true); \
	if [ -n "$$extra" ]; then echo "FAIL: a second SPDX form in the imported tree:"; printf '%s\n' "$$extra"; exit 1; fi; \
	mine="$$(find $(PORT) $(SEAM) -name '*.c' -o -name '*.h') $(BOARD)/dwc_mmc.c $(BOARD)/dwc_mmc.h $(BOARD)/dwc_mmc_regs.h $(BOARD)/dwc_mshc.c $(BOARD)/dwc_mshc.h $(BOARD)/dwc_mshc_regs.h $(BOARD)/dwc_mshc_dma.h"; \
	bad=$$(grep -L 'SPDX-License-Identifier: BSD-3-Clause' $$mine || true); \
	if [ -n "$$bad" ]; then echo "FAIL: a first-party file without the project SPDX line:"; printf '%s\n' "$$bad"; exit 1; fi; \
	copy=$$(grep -l 'GNU General Public' $$mine || true); \
	if [ -n "$$copy" ]; then echo "FAIL: license text copied into a first-party file:"; printf '%s\n' "$$copy"; exit 1; fi; \
	echo "spdx: $$n imported files BSD-3-Clause with no second form; $$(printf '%s\n' $$mine | wc -l) first-party files with the project SPDX line and no copied license text"

# ---- gates: kernel isolation over the objects and the author-side sources -----

gates: $(OBJS)
	PATH=$(dir $(CROSS_COMPILE)):$$PATH \
	sh $(ROOT)/tools/gates/check-kernel-isolation.sh \
	    --root "$(ROOT)" \
	    --stack-objs "$(BUILD)" \
	    --sources "$(PORT) $(SEAM) $(BOARD)" \
	    --canary-src "$(PORT)" \
	    --canary

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)
