#!/bin/sh
# Trim probe: a tick-only image must not drag in the network stack, and a full
# image must actually contain it.  This is deliberately a symbol/size probe,
# not a byte-for-byte cap (the real cap follows once the libraries land).
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cross=${AARCH64_CROSS_PATH:-}
tick=rtos-shell
full=net-netio-raw-tx256
size_of() {
	${cross:+$cross/bin/}aarch64-none-elf-size -A "$1" | awk '/^\.text|^\.rodata|^\.data|^\.bss/{s+=$2} END{print s+0}'
}
"$root/make.rtos" config PROFILE=$tick >/dev/null
"$root/make.rtos" clean >/dev/null 2>&1
"$root/make.rtos" image PROFILE=$tick >/dev/null
tick_elf=$root/tests/board/rtos/build/build/rtos-freertos.elf
tick_size=$(size_of "$tick_elf")
if ${cross:+$cross/bin/}aarch64-none-elf-nm "$tick_elf" | grep -qE ' (lwip_init|netif_add|tcp_input)$'; then
	echo "FAIL: $tick carries lwIP/network symbols" >&2; exit 1
fi
"$root/make.rtos" clean >/dev/null 2>&1
"$root/make.rtos" image PROFILE=$full >/dev/null
full_elf=$root/tests/board/rtos/build/build/rtos-freertos.elf
full_size=$(size_of "$full_elf")
if ! ${cross:+$cross/bin/}aarch64-none-elf-nm "$full_elf" | grep -qE ' (lwip_init|netif_add)$'; then
	echo "FAIL: $full lacks lwIP symbols" >&2; exit 1
fi
if [ "$tick_size" -ge "$full_size" ]; then
	echo "FAIL: tick image ($tick_size) is not smaller than full ($full_size)" >&2; exit 1
fi
echo "trimcheck: ok tick=$tick_size full=$full_size"
