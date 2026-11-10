# platform/os: the OS-agnostic runtime services that sit under the backend.
#
# These four compile in the platform's world for every image; the ThreadX
# firefly C-library bindings are backend+board glue and stay in the backend's
# source list (BOARD_TX_EXTRA_SRCS), where their link order already was.

obj-y += \
	baremetal/printf_bind.c \
	baremetal/libc_stubs.c \
	baremetal/log.c \
	baremetal/tick.c
