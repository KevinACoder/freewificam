#!/bin/sh
# Gate: kernel-interface isolation for the first-party port layers.
#
# Covered port trees: ports/net80211 (the wireless stack's port),
# ports/cherryusb (the USB host stack's board layer, osal and class
# application half) and ports/lwip (the TCP/IP stack's OS adapter, enabled by
# --lwip-port). Each is compiled here under --port-objs/--sources; the cross32
# gate carries a per-tree translation-unit list.
#
# The charter rule: components reach the kernel only through CMSIS-RTOS2, so no
# object or source of this project may name another kernel's API. Three gates:
#
#   1. symbol gate - nm over the imported stack objects (as compiled here) and the
#      port objects: zero hits for tx_*, TX_*, xTask*/xQueue*/xSemaphore*/xEventGroup*,
#      vTask*/vQueue*, pthread_*. The port layer's own objects additionally may not
#      reference any of them either.
#   2. source gate - word scan of the author-side sources (ports/net80211/** and the
#      contract directory) for the same names. The imported component bytes are not
#      scanned here (they are a verbatim copy; the symbol gate covers their objects),
#      matching the exclusion rule of check-local-knowledge.sh.
#   3. cross32 gate - the port layer and the stack compile for a 32-bit target
#      (arm-none-eabi, cortex-m4), compile-only: the project's "no 64-bit
#      assumptions" rule made mechanical.
#
# With --canary each gate is proven able to go red: an offender is injected, the
# gate must fail, the offender is removed and the gate must pass again.
#
# Usage:
#   check-kernel-isolation.sh --stack-objs DIR --port-objs DIR --sources "DIR DIR" [--canary]
#                               [--lwip-port]
#
# Exit status: 0 clean, 1 findings or a failed canary proof.

set -eu

stack_objs=
port_objs=
sources=
canary=0
lwip_port=0

while [ $# -gt 0 ]; do
	case "$1" in
	--stack-objs) stack_objs=$2; shift 2 ;;
	--port-objs) port_objs=$2; shift 2 ;;
	--sources) sources=$2; shift 2 ;;
	--canary) canary=1; shift ;;
	--lwip-port) lwip_port=1; shift ;;
	*) echo "check-kernel-isolation: unknown argument $1" >&2; exit 2 ;;
	esac
done

if [ -z "$stack_objs" ] || [ -z "$sources" ]; then
	echo "check-kernel-isolation: --stack-objs and --sources are required" >&2
	exit 2
fi

self=$(pwd)/$(basename "$0")
# Kernel API names of the kernels this project does not use in components. The
# pattern is deliberately word-anchored so identifiers like "txt" do not match.
forbidden='(^|[^A-Za-z0-9_])(_tx_[a-z]|tx_[a-z]|TX_[A-Z]|xTask|xQueue|xSemaphore|xEventGroup|xTimer|vTask|vQueue|vSemaphore|vEventGroup|pthread_)'

status=0

report() {
	if [ -n "$1" ]; then
		printf '%s\n' "$1"
		echo "check-kernel-isolation: FAIL ($2)"
		status=1
	else
		echo "check-kernel-isolation: PASS ($2)"
	fi
}

symbol_scan() {
	# $1 = optional extra object directory (the canary)
	extra=${1:-}
	objs=$(find "$stack_objs" ${port_objs:+"$port_objs"} $extra -name '*.o' 2>/dev/null | sort)
	if [ -z "$objs" ]; then
		echo ""
		return
	fi
	# shellcheck disable=SC2086
	nm -u $objs 2>/dev/null | awk 'NF > 0 {print $NF}' | sort -u \
		| grep -nE "$forbidden" || true
}

source_scan() {
	# shellcheck disable=SC2086
	find $sources -name '*.c' -o -name '*.h' | grep -v "^$self$" \
		| xargs -r grep -nHE "$forbidden" || true
}

cross32_cherryusb() {
	# The first-party USB port files: the osal (one CMSIS-RTOS2 backend for both
	# kernels) and the class application half. Compile-only, same rule as the
	# wireless tree; the board-specific glue and the hardware sequence are
	# excluded - they legitimately touch this board's registers and timers.
	root=$1
	cfg=$root/ports/cherryusb/board/rk3568-itx
	cu=$root/third-party/cherryusb
	flags="-std=c11 -Wall -DCMSIS2_BACKEND_THREADX
	    -I$root -I$root/os/cmsis2 -I$root/os/baremetal -I$cfg
	    -I$cu/common -I$cu/core -I$cu/class/hub -I$cu/class/msc
	    -I$root/third-party/CMSIS_6/CMSIS/RTOS2/Include"
	# shellcheck disable=SC2086
	for f in $root/ports/cherryusb/osal/cmsis2/osal_cmsis2.c \
	    $root/ports/cherryusb/class/msc_app.c; do
		$CROSS -mcpu=cortex-m4 -mthumb $flags -fsyntax-only "$f" 2>&1 \
			| grep -E 'error:' || true
	done
}

CROSS=${CROSS:-arm-none-eabi-gcc}

cross32_lwip() {
	# The lwIP port layer: the OS adapter (the only file that names
	# CMSIS-RTOS2) and the platform hook file, plus a slice of the imported
	# stack so the "no 64-bit assumptions" rule covers both sides. Compile
	# only, syntax level: the gate proves 32-bit compilability, not a link.
	root=$1
	cmsis=$root/third-party/CMSIS_6/CMSIS/RTOS2/Include
	flags="-std=c11 -Wall -DCMSIS2_BACKEND_THREADX
	    -I$root/ports/lwip/include -I$root/third-party/lwip/src/include
	    -I$root/third-party/lwip/contrib/apps/ping
	    -I$root -I$root/os/cmsis2 -I$root/os/baremetal -I$root/arch/aarch64
	    -I$cmsis"
	# shellcheck disable=SC2086
	for f in $root/ports/lwip/sys_arch.c $root/ports/lwip/platform_hooks.c \
	    $root/third-party/lwip/src/core/pbuf.c \
	    $root/third-party/lwip/src/core/netif.c \
	    $root/third-party/lwip/src/apps/lwiperf/lwiperf.c; do
		$CROSS -mcpu=cortex-m4 -mthumb $flags -fsyntax-only "$f" 2>&1 \
			| grep -E 'error:' || true
	done
}

cross32_scan() {
	# The port layer and the stack, syntax only, 32-bit target. The flags mirror
	# the contract build; the CMSIS include comes from the vendored CMSIS_6.
	root=$1
	flags="-std=gnu11 -Wall -Wno-unused-function -Wno-pointer-sign -D_KERNEL
	    -include $root/ports/net80211/compat/prelude.h
	    -I$root/ports/net80211/compat -I$root/components/net80211/compat/netbsd
	    -I$root/components/net80211 -I$root/third-party/CMSIS_6/CMSIS/RTOS2/Include"
	# shellcheck disable=SC2086
	for f in $root/ports/net80211/net/*.c $root/ports/net80211/osal/cmsis2/*.c \
	    $root/components/net80211/net80211/ieee80211.c \
	    $root/components/net80211/net80211/ieee80211_netbsd.c \
	    $root/components/net80211/net80211/ieee80211_node.c \
	    $root/components/net80211/net80211/ieee80211_proto.c \
	    $root/components/net80211/net80211/ieee80211_output.c \
	    $root/components/net80211/net80211/ieee80211_input.c \
	    $root/components/net80211/net80211/ieee80211_amrr.c \
	    $root/components/net80211/net80211/ieee80211_crypto.c \
	    $root/components/net80211/net80211/ieee80211_crypto_none.c \
	    $root/components/net80211/net80211/ieee80211_crypto_ccmp.c \
	    $root/components/net80211/crypto/aes/aes_ccm_mbuf.c \
	    $root/components/net80211/port/aes_impl_compat.c; do
		$CROSS -mcpu=cortex-m4 -mthumb $flags -fsyntax-only "$f" 2>&1 \
			| grep -E 'error:' || true
	done
}

# ---- the three gates --------------------------------------------------------

report "$(symbol_scan)" "symbol gate (nm: no foreign kernel API)"
report "$(source_scan)" "source gate (word scan of the author-side sources)"

if command -v "$CROSS" >/dev/null 2>&1; then
	root=$(cd "$(dirname "$sources")" 2>/dev/null && pwd || true)
	# The sources argument carries the port directory first; the repo root is three
	# levels up from tests/host/<dir>.
	first=$(printf '%s' "$sources" | cut -d' ' -f1)
	if [ -n "$first" ]; then
		root=$(cd "$first/../.." 2>/dev/null && pwd)
	else
		root=""
	fi
	if [ -n "$root" ] && [ -d "$root/components/net80211" ]; then
		report "$(cross32_scan "$root")" "cross32 gate (arm-none-eabi, cortex-m4)"
	else
		report "cannot locate the repository root for the cross32 gate" "cross32 gate"
	fi
	if [ -n "$root" ] && [ -d "$root/ports/cherryusb" ]; then
		report "$(cross32_cherryusb "$root")" "cross32 gate, cherryusb port (arm-none-eabi, cortex-m4)"
	fi
	if [ "$lwip_port" = 1 ] && [ -n "$root" ] && [ -d "$root/ports/lwip" ]; then
		report "$(cross32_lwip "$root")" "cross32 gate, lwip port (arm-none-eabi, cortex-m4)"
	fi
else
	echo "check-kernel-isolation: BLOCKED (cross32 gate: $CROSS not on PATH)"
fi

# ---- canary proofs ---------------------------------------------------------

if [ "$canary" = 1 ]; then
	tmp=$(mktemp -d)
	trap 'rm -rf "$tmp"; rm -f "$stack_objs/.canary-isolation.o" "$port_src_canary"' EXIT HUP INT TERM

	# 1. symbol gate canary: a real object that calls another kernel's API.
	port_src_canary=
	for d in $sources; do
		if [ -d "$d" ] && [ "$d" != "$stack_objs" ]; then
			case "$d" in ports/*|*/ports/*) port_src_canary="$d/.canary-isolation.c" ;; esac
		fi
	done
	printf 'void *canary(void) { return (void *) pthread_self(); }\n' > "$tmp/canary.c"
	cc -c "$tmp/canary.c" -o "$stack_objs/.canary-isolation.o" 2>/dev/null || true
	if [ -f "$stack_objs/.canary-isolation.o" ]; then
		if [ -n "$(symbol_scan)" ]; then
			echo "check-kernel-isolation: PASS (symbol gate canary went red)"
		else
			echo "check-kernel-isolation: FAIL (symbol gate canary stayed green)"
			status=1
		fi
		rm -f "$stack_objs/.canary-isolation.o"
	else
		echo "check-kernel-isolation: BLOCKED (symbol gate canary: cannot compile the offender)"
	fi

	# 2. source gate canary.
	if [ -n "$port_src_canary" ]; then
		printf '/* canary */ void *c(void) { return (void *) pthread_self(); }\n' > "$port_src_canary"
		if [ -n "$(source_scan)" ]; then
			echo "check-kernel-isolation: PASS (source gate canary went red)"
		else
			echo "check-kernel-isolation: FAIL (source gate canary stayed green)"
			status=1
		fi
		rm -f "$port_src_canary"
	else
		echo "check-kernel-isolation: BLOCKED (source gate canary: no port source directory in --sources)"
	fi

	# 3. cross32 gate canary: an object that only compiles on a 64-bit target.
	printf 'int canary64[sizeof(void *) == 8 ? 1 : -1];\n' > "$tmp/canary64.c"
	if [ -n "$root" ] && [ -d "$root/ports/net80211" ]; then
		if "$CROSS" -mcpu=cortex-m4 -mthumb -fsyntax-only "$tmp/canary64.c" 2>&1 | grep -q 'error:'; then
			echo "check-kernel-isolation: PASS (cross32 gate canary went red)"
		else
			echo "check-kernel-isolation: FAIL (cross32 gate canary stayed green)"
			status=1
		fi
	fi
fi

exit $status
