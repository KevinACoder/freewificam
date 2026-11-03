# Component source lists: the one include helper.
#
# A component owns exactly one sources.mk, written in kbuild shape:
#
#     obj-$(CONFIG_FOO) += path/relative/to/the/component.c
#
# The generated make config writes a boolean as 1/0 (make cannot branch on
# "y"), so the helper folds the `obj-1` list the expansion produced into
# `obj-y`.  A disabled symbol leaves an `obj-0` list that is ignored.
#
# For the few files that compile in a different flag world than the rest of the
# component, one of the named lists:
#
#     kpi-obj-$(CONFIG_BAR) += seam.c        # linuxkpi flag world
#     case-obj-$(CONFIG_BAR) += ../app/cases/x.c
#     port-obj-$(CONFIG_BAR) += port.c      # bus port flag world
#     app-obj-$(CONFIG_BAR)  += app.c       # 8189FS application world
#     sdio-obj-$(CONFIG_BAR) += host.c      # 8189FS SDIO world
#     drv-obj-$(CONFIG_BAR)  += driver.c    # vendor driver world
#     lwip-obj-$(CONFIG_BAR) += stack.c     # lwIP world
#
# A component file is included through
#
#     $(eval $(call component_sources,<dir>,<tag>,<NAME>))
#
# where <tag> selects an ifeq ($(SRC_TAG),<tag>) block (a component with one
# world can ignore the tag).  The helper prefixes relative paths with <dir>,
# keeps absolute and $(ROOT)-based paths alone, and publishes:
#
#     <NAME>_SRCS, <NAME>_KPI_SRCS, <NAME>_CASE_SRCS, <NAME>_PORT_SRCS,
#     <NAME>_APP_SRCS, <NAME>_SDIO_SRCS, <NAME>_DRV_SRCS, <NAME>_LWIP_SRCS
#
# Every source path appears in exactly one list.

# Prefix the relative words of a source list with the component directory.
# $(1) = component root, $(2) = words.
src_paths = $(foreach f,$(2),$(if $(filter /%,$(f)),$(f),$(1)/$(f)))

define component_sources
obj-y :=
obj-1 :=
kpi-obj-y :=
kpi-obj-1 :=
case-obj-y :=
port-obj-y :=
app-obj-y :=
sdio-obj-y :=
drv-obj-y :=
lwip-obj-y :=
SRC_TAG := $(2)
include $(1)/sources.mk
obj-y += $$(obj-1)
kpi-obj-y += $$(kpi-obj-1)
case-obj-y += $$(case-obj-1)
port-obj-y += $$(port-obj-1)
app-obj-y += $$(app-obj-1)
sdio-obj-y += $$(sdio-obj-1)
drv-obj-y += $$(drv-obj-1)
lwip-obj-y += $$(lwip-obj-1)
$(3)_SRCS := $$(call src_paths,$(1),$$(obj-y))
$(3)_KPI_SRCS := $$(call src_paths,$(1),$$(kpi-obj-y))
$(3)_CASE_SRCS := $$(call src_paths,$(1),$$(case-obj-y))
$(3)_PORT_SRCS := $$(call src_paths,$(1),$$(port-obj-y))
$(3)_APP_SRCS := $$(call src_paths,$(1),$$(app-obj-y))
$(3)_SDIO_SRCS := $$(call src_paths,$(1),$$(sdio-obj-y))
$(3)_DRV_SRCS := $$(call src_paths,$(1),$$(drv-obj-y))
$(3)_LWIP_SRCS := $$(call src_paths,$(1),$$(lwip-obj-y))
obj-y :=
obj-1 :=
kpi-obj-y :=
kpi-obj-1 :=
case-obj-y :=
case-obj-1 :=
port-obj-y :=
port-obj-1 :=
app-obj-y :=
app-obj-1 :=
sdio-obj-y :=
sdio-obj-1 :=
drv-obj-y :=
drv-obj-1 :=
lwip-obj-y :=
lwip-obj-1 :=
endef
