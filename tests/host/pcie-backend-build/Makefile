# Static build gates for the PCIe backend line: the GICv3 ITS + message-interrupt
# domain (arch/aarch64), the DesignWare root-complex core and its board backend,
# MSI-X table programming and the NVMe block driver, against the board port layer
# they bind to.
#
# Self contained; run it from anywhere with:
#   make -C <repo>/tests/host/pcie-backend-build
#
# The line is all first-party: the units are this project's own board/arch
# drivers, and the port TUs they bind to are the project's own base layer. The
# checks still split along the same four faces the other port-build suites use:
#
#   compile    every unit of the line plus the port TUs it binds to compile for
#              aarch64, at the project's own warning level (-Wall -Wextra), and a
#              warning in any file is a failure - there is no allowlist, because
#              none was needed when this gate was written. If a warning ever
#              appears it should be recorded here with its file and line, the way
#              the driver port-build suites do it, rather than silenced.
#   contract   the line's headers and the port headers they bridge to compile as
#              one translation unit on their own, so the faces cannot hide behind
#              the bytes that consume them, and the constants the layer above
#              depends on are asserted rather than assumed.
#   spdx       the license split is mechanical: every file this line owns (the
#              arch ITS/MSI units, the board PCIe/NVMe drivers and their headers,
#              and the contract unit) carries exactly one SPDX form
#              (BSD-3-Clause) and no license text has been copied from elsewhere.
#              The pre-existing port TUs (gicv3.c, cache.c, log.c, minilibc.c) are
#              repository files with their own provenance and are not scanned.
#   gates      the kernel-isolation gates over the built objects and the
#              author-side sources, with their canary proofs.
#
# The "port TUs" are the base-layer translation units this line's headers bridge
# onto: arch/aarch64/gicv3.c (the IRQ registry behind the CMSIS IRQ_* bridge),
# cache.c (the cache maintenance the NVMe driver calls) and
# os/baremetal/{log,minilibc}.c (the console sink and the formatter).

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT  := $(abspath $(MAKEFILE_DIR)/../../..)
PCIE  := $(ROOT)/board/rk3568-itx/drivers/pcie
NVME  := $(ROOT)/board/rk3568-itx/drivers/nvme
ARCH  := $(ROOT)/arch/aarch64
OSBM  := $(ROOT)/os/baremetal
CMSIS := $(ROOT)/third-party/CMSIS_6/CMSIS
BUILD := $(MAKEFILE_DIR)/build

AARCH64_CROSS_PATH ?= $(HOME)/workspace/env/src/xpack-aarch64-none-elf-gcc-13.2.1-1.1
CROSS_COMPILE ?= $(AARCH64_CROSS_PATH)/bin/aarch64-none-elf-
CC  := $(CROSS_COMPILE)gcc
NM  := $(CROSS_COMPILE)nm

# The line's own units: the ITS + message-interrupt domain and the CMSIS IRQ
# bridge (arch), the root-complex core, MSI-X table programming and the board
# PCIe backend, and the NVMe driver.
LINE_SRCS := \
	$(ARCH)/gicv3_its.c \
	$(ARCH)/gicv3_msi.c \
	$(ARCH)/irq_ctrl.c \
	$(PCIE)/pcie_dw.c \
	$(PCIE)/dwc_msix.c \
	$(PCIE)/dwc_msi.c \
	$(PCIE)/dwc_pcie.c \
	$(NVME)/dwc_nvme.c

# The first-party port TUs the line binds to: the arch IRQ registry and cache
# maintenance, and the bare-metal console sink and formatter.
PORT_SRCS := \
	$(ARCH)/gicv3.c \
	$(ARCH)/cache.c \
	$(OSBM)/log.c \
	$(OSBM)/minilibc.c

SRCS := $(LINE_SRCS) $(PORT_SRCS)

ARCHFLAGS := -mcpu=cortex-a55 -mgeneral-regs-only -mstrict-align -mno-outline-atomics
COMMON := -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -g3 -O2 \
    -ffunction-sections -fdata-sections
# The line's headers are reached by bare name from the driver directories
# (dwc_pcie.h, nvme.h) and from arch (msi.h, gicv3_its.h, regs.h); CMSIS supplies
# the Driver_Common.h vocabulary and the Core(A) irq_ctrl.h the drivers speak.
INC := \
    -I$(PCIE) -I$(NVME) -I$(ARCH) \
    -I$(ROOT)/board/rk3568-itx -I$(ROOT)/board/rk3568-itx/drivers \
    -I$(ROOT)/os/baremetal -I$(ROOT)/os/cmsis2 \
    -I$(ROOT)/components/ulog/src \
    -I$(CMSIS)/Driver/Include \
    -I$(CMSIS)/Core/Include/a-profile \
    -I$(CMSIS)/RTOS2/Include \
    -I$(ROOT)
KEYS := -DCMSIS2_BACKEND_THREADX -DULOG_ENABLED -DULOG_MAX_MESSAGE_LENGTH=256 \
    -DCMSIS_device_header='"cmsis_os2.h"'
# Header dependencies are tracked: the headers are edited far more often than
# the driver bytes, and a stale object would report the previous header's warnings.
DEPFLAGS := -MMD -MP

CFLAGS := $(COMMON) -Wall -Wextra

.PHONY: all compile contract spdx gates clean

all: compile contract spdx gates

# ---- compile: every unit of the line plus the port TUs, aarch64 ---------------

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

$(BUILD)/%.o: $(ARCH)/%.c | $(BUILD)
	@set -e; \
	$(CC) $(ARCHFLAGS) $(CFLAGS) $(KEYS) $(DEPFLAGS) $(INC) -c $< -o $@ 2> $(BUILD)/$*.compile.log || \
		{ cat $(BUILD)/$*.compile.log; exit 1; }

$(BUILD)/%.o: $(PCIE)/%.c | $(BUILD)
	@set -e; \
	$(CC) $(ARCHFLAGS) $(CFLAGS) $(KEYS) $(DEPFLAGS) $(INC) -c $< -o $@ 2> $(BUILD)/$*.compile.log || \
		{ cat $(BUILD)/$*.compile.log; exit 1; }

$(BUILD)/%.o: $(NVME)/%.c | $(BUILD)
	@set -e; \
	$(CC) $(ARCHFLAGS) $(CFLAGS) $(KEYS) $(DEPFLAGS) $(INC) -c $< -o $@ 2> $(BUILD)/$*.compile.log || \
		{ cat $(BUILD)/$*.compile.log; exit 1; }

$(BUILD)/%.o: $(OSBM)/%.c | $(BUILD)
	@set -e; \
	$(CC) $(ARCHFLAGS) $(CFLAGS) $(KEYS) $(DEPFLAGS) $(INC) -c $< -o $@ 2> $(BUILD)/$*.compile.log || \
		{ cat $(BUILD)/$*.compile.log; exit 1; }

# ---- contract: the faces on their own -----------------------------------------

contract: $(BUILD)/face_contract.o
	@set -e; \
	log=$$(cat $(BUILD)/face_contract.compile.log 2>/dev/null || true); \
	warn=$$(printf '%s\n' "$$log" | grep -c 'warning:' || true); \
	echo "contract: the line's headers and the port headers compile standalone, warning lines: $$warn"; \
	if [ "$$warn" != "0" ]; then \
		echo "FAIL: a face must compile without warnings of its own:"; \
		printf '%s\n' "$$log" | grep 'warning:'; \
		exit 1; \
	fi

$(BUILD)/face_contract.o: $(MAKEFILE_DIR)/face_contract.c | $(BUILD)
	@set -e; \
	$(CC) $(ARCHFLAGS) $(CFLAGS) $(KEYS) $(DEPFLAGS) $(INC) -c $< -o $@ 2> $(BUILD)/face_contract.compile.log || \
		{ cat $(BUILD)/face_contract.compile.log; exit 1; }

-include $(wildcard $(BUILD)/*.d)

# ---- spdx: the license split, mechanically ------------------------------------

# Every file this line owns, listed explicitly: the arch ITS/MSI units and the
# IRQ bridge, the board PCIe/NVMe drivers and their headers, and the contract
# unit. Each must carry the project SPDX line and no copied license text.
SPDX_FILES := \
    $(ARCH)/gicv3_its.c $(ARCH)/gicv3_its.h $(ARCH)/gicv3_msi.c $(ARCH)/msi.h \
    $(ARCH)/irq_ctrl.c \
    $(PCIE)/pcie_dw.c $(PCIE)/pcie_dw.h $(PCIE)/dwc_msix.c $(PCIE)/dwc_msix.h \
    $(PCIE)/dwc_msi.c $(PCIE)/dwc_msi.h \
    $(PCIE)/dwc_pcie.c $(PCIE)/dwc_pcie.h \
    $(NVME)/dwc_nvme.c $(NVME)/dwc_nvme.h $(NVME)/dwc_nvme_regs.h $(NVME)/nvme.h \
    $(MAKEFILE_DIR)/face_contract.c

spdx:
	@set -e; \
	n=$$(printf '%s\n' $(SPDX_FILES) | wc -l); \
	bad=$$(grep -L 'SPDX-License-Identifier: BSD-3-Clause' $(SPDX_FILES) || true); \
	if [ -n "$$bad" ]; then echo "FAIL: a first-party file without the project SPDX line:"; printf '%s\n' "$$bad"; exit 1; fi; \
	extra=$$(grep -h 'SPDX-License-Identifier' $(SPDX_FILES) | sort -u | grep -v 'BSD-3-Clause' || true); \
	if [ -n "$$extra" ]; then echo "FAIL: a second SPDX form in the line:"; printf '%s\n' "$$extra"; exit 1; fi; \
	copy=$$(grep -l 'GNU General Public' $(SPDX_FILES) || true); \
	if [ -n "$$copy" ]; then echo "FAIL: license text copied into a first-party file:"; printf '%s\n' "$$copy"; exit 1; fi; \
	echo "spdx: $$n first-party files BSD-3-Clause with no second form and no copied license text"

# ---- gates: kernel isolation over the objects and the author-side sources -----

gates: $(OBJS)
	PATH=$(dir $(CROSS_COMPILE)):$$PATH \
	sh $(ROOT)/tools/gates/check-kernel-isolation.sh \
	    --root "$(ROOT)" \
	    --stack-objs "$(BUILD)" \
	    --sources "$(PCIE) $(NVME) $(ARCH) $(MAKEFILE_DIR)" \
	    --canary-src "$(MAKEFILE_DIR)" \
	    --canary

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)
