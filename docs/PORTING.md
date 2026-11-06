# Porting freewificam

This document is the contract for a consumer that builds against the published
freewificam SDK.  It is written for the L1 checkpoint (M5): the public headers,
the per-profile static libraries, `pkg-config`, and a link-only consumer gate.
It deliberately does not claim the L2–L5 milestones; §8 lists what each would
still require.

The short version:

* one profile is one SDK and one ABI; the configuration header is generated,
  never passed on the command line;
* a consumer includes only `<freewificam/...>` and links only through
  `freewificam.pc`;
* a log/configuration consumer links completely today (measured, two profiles);
* a Wi-Fi control consumer compiles, but a useful link needs the profile's
  link-forcing flags plus the consumer's CMSIS-RTOS2 implementation and linker
  script (measured on one wireless profile);
* the OS, the network stack and the physical bus adapter are the consumer's
  side of the port.  The SDK does not ship an RTOS.

## 1. Building an SDK

```
./make.rtos sdk PROFILE=net-netio-raw-tx256
./make.rtos sdk-check PROFILE=net-netio-raw-tx256
```

`sdk` depends on `libs`, which depends on `image`.  The image is still built
first: the archives are a delivery shape of the objects that already passed the
image gates, not a second, independently configured build.  `sdk-check` runs
`sdk` and then `tests/host/sdk-consumer/check.sh`.

The result is a self-contained tree:

```
dist/<profile>/
  include/freewificam/
    fw_config.h fw_osal.h fw_bus.h fw_netif.h fw_wifi.h fw_log.h fw_core.h
    freewificam_config.h          # generated for this profile
    deps/                         # exact component headers (see §2)
  lib/
    libfw_*.a                     # the archives selected for this profile
    manifest.json
  manifest.json
  freewificam.pc
  freewificam.registration        # the profile's -Wl,-u set (see §5)
  PORTING.md
```

`dist/` is generated and ignored by git.  Nothing in it may be edited by hand:
rebuild it for another profile instead.

### Firmware blobs

The RTOS image has no filesystem, so a driver firmware is a C array under
`components/linuxkpi/fw/`.  The vendor bytes are committed under `firmware/`
(see its `README.md`); the arrays are generated from them by
`tools/rtos/fw-blobs.py`, which the build runs by itself, so a clean checkout
needs no manual step:

```
tools/rtos/fw-blobs.py --all          # what the build does, by profile source
tools/rtos/fw-blobs.py --check --all  # arrays vs firmware/, and their digests
```

`docs/FIRMWARE-NOTICES.md` carries the per-chip digests, the source each file
came from and the redistribution terms.  A chip this repository does not carry
uses the single-file form, `tools/rtos/fw-blob.py`.  This is a build
prerequisite, not a consumer concern: the array is inside `libfw_linuxkpi.a`
(via the registration object that the profile's link flags force in).

## 2. The public headers

| Header | Status in this checkpoint | What it carries |
|---|---|---|
| `fw_config.h` | stable, required | the generated configuration, no fallbacks |
| `fw_log.h` | ready, links and is covered by the consumer gate | formatted console lines, levels, raw bytes, emergency print |
| `fw_osal.h` | ABI only; the consumer implements it | CMSIS-RTOS2 + four project extensions |
| `fw_wifi.h` | control API ready at source/archive level; link caveat in §5 | port up/down, scan, hardware address, diagnostics, events |
| `fw_netif.h` | data hooks ready; no netif registration | RX/EAPOL callbacks and `wlan_port_xmit()` |
| `fw_bus.h` | **not ready** (`FW_BUS_API_READY 0`) | bus-neutral identity only; no transfer submission |
| `fw_core.h` | **lifecycle not ready** (`FW_CORE_API_READY 0`) | configuration + OS ABI; no `fw_init()`/`fw_run()` |

Rules for all of them:

* include only `<freewificam/fw_*.h>`.  `freewificam/deps/` contains the exact
  implementation headers the wrappers forward (`ulog.h`, the baremetal
  `log.h`, the net80211 `port.h`, `cmsis_os2.h`, `cmsis_os2_ext.h`).  They are
  shipped so the declarations cannot drift from the archived objects, not as a
  second public API; include them directly only if a wrapper does not expose
  something you have agreed to use.
* every header compiles alone against `dist/<profile>/include` with a host
  compiler.  That is checked for all seven by `sdk-check`; no internal include
  path is needed or permitted.
* the headers name no RTOS, no lwIP type and no CherryUSB type.  `fw_osal.h`
  names CMSIS-RTOS2 because that is the ABI of the libraries; the
  implementation is the consumer's.

`fw_log.h` forwards the real console implementation.  The stable surface is
the level macros (`LOG_TRACE` … `LOG_CRITICAL`, `LOG_ANCHOR`), `log_emit()`,
`log_level_get()`/`log_level_set()`, `board_console_write_raw()`,
`board_log_emergency()` and `board_log_stamp_use_tick()`.  The sink assembles
whole lines, drops `\r`, owns CR LF, and is not re-entrant; a fault path must
use `board_log_emergency()`.  The macros compile to nothing when the image has
no `ULOG_ENABLED`, so a log call must not carry a side effect.

`fw_wifi.h` is the net80211 port's control surface.  It is real and
implemented, but it is not a complete supplicant interface: `wlan_port_scan()`
currently ignores its SSID argument, there is no named "join this BSS" call,
no AP/IBSS start, and no channel/HT configuration.  Association is driven by
the port state machine and the EAPOL hook.  `wlan_port_get_ic()` is an opaque
escape hatch for the in-tree supplicant bridge, not an object model.

`fw_netif.h` is the data seam: the consumer registers `wlan_port_set_data_rx()`
and `wlan_port_set_eapol_rx()`, sends with `wlan_port_xmit()`, and wires those
to its own network stack.  RX buffers are borrowed for the duration of the
callback; there is no zero-copy hand-off and no TX completion report.  There is
no `fw_netif_register()`: the consumer owns its `struct netif` (the in-tree
example is `tests/board/rtos/app/cases/wlan_netif.c`).

`fw_bus.h` and `fw_core.h` are intentionally almost empty.  The USB path is
still an internal linuxkpi/CherryUSB shape and the image entry is
application-owned (`tests/board/rtos/app/main.c`).  Publishing either as an API
now would freeze the wrong boundary; the headers document what L3/L2 must
define instead of inventing placeholders.

## 3. The configuration rule

There is exactly one configuration channel: the generated header.

```
Kconfig defconfig
  -> tools/build/kconfig-gen.py
  -> build/kconfig/<profile>/freewificam_config.h
  -> installed as dist/<profile>/include/freewificam/freewificam_config.h
  -> included by include/freewificam/fw_config.h
```

A consumer writes:

```c
#include <freewificam/fw_config.h>

#if defined(CONFIG_FOO)
...
#endif
```

and must not:

* pass `-D CONFIG_...` (or any other configuration macro) on the command
  line.  The no-`-D` rule is a build invariant; a consumer that adds one can
  silently compile against different values than the archives it links.
* copy `freewificam_config.h` from another profile, or mix headers from one
  profile with libraries from another.  Two profiles are two ABIs.
* rely on a default for a symbol the defconfig did not set.  The generated
  header carries a `CONFIG_X is not set` comment, not a zero value; use
  `#if defined(...)`.
* include `freewificam_config.h` by name as a substitute for `fw_config.h`.
  The generated file is an installation detail; `fw_config.h` is the entry
  point and it fails loudly if the generated header is missing or from an
  older generator.

`fw_config.h` requires `CONFIG_PROFILE_NAME`, `CONFIG_BOARD_NAME` and
`CONFIG_BACKEND_NAME`; a missing one is a compile error.  The generated header
also carries the legacy macro aliases that vendored code expects (for example
`ULOG_ENABLED`); those aliases are part of the generated contract.

There is no cross-profile compatibility promise, no stable binary ABI and no
version negotiation in this checkpoint.  A consumer that needs another
configuration rebuilds the SDK for that profile.

## 4. Library selection

`manifest.json` (schema 1) lists, for the profile: each archive, its member
count, its total size and the undefined symbols of its members.  `libs` groups
the image's existing object files; it does not recompile anything.  Which
archives exist depends on what the profile compiled:

| Archive | Source group | Wired profile | Wireless profile |
|---|---|---|---|
| `libfw_app.a` | `tests/board/rtos/app/` | yes | yes |
| `libfw_net80211.a` | `components/net80211/` | no | yes |
| `libfw_linuxkpi.a` | `components/linuxkpi/` | no | yes |
| `libfw_usbhost.a` | `third-party/cherryusb/` (the OSAL), `components/linuxkpi/rtos/usb/` (the linuxkpi backend), `tests/board/rtos/platform/board/rk3568-itx/usb/` and `tests/board/rtos/app/usb/` | no | yes |
| `libfw_drivers.a` | `wireless-drivers/` | no | yes |
| `libfw_board_rk3568-itx.a` | `platform/board/` | yes | yes |
| `libfw_osal_cmsis2.a` | `platform/os/{cmsis2,baremetal}/` | yes | yes |
| `libfw_support.a` | `third-party/{ulog,printf}/` | yes | yes |
| `libfw_arch.a` | `platform/arch/` | yes | yes |

Two measured examples:

* `net-netio-raw-tx256` — 5 archives (`app`, `board`, `osal`, `support`,
  `arch`);
* `rtl8xxxu-8723bu-netio-ar-ht20-ba` — 9 archives (the five above plus
  `net80211`, `linuxkpi`, `usbhost`, `drivers`).

What is deliberately **not** in the SDK:

* the RTOS: FreeRTOS/ThreadX/CMSIS-RTOS2 implementations are the consumer's
  (`third-party/CMSIS-FreeRTOS` in the in-tree image, a ThreadX adapter in
  `platform/os/threadx`).  Vendored kernel objects are not archived.  A
  consumer must provide the CMSIS-RTOS2 calls in §5.
* lwIP: the network stack belongs to the consumer side of `fw_netif.h`.
* the bare-metal start-up, linker script and cache/memory symbols.  The SDK is
  an archive set, not an image.
* the app runner and the board test table: `libfw_app.a` is shipped for image
  parity, but its `main`/`BOARD_TESTS` section is not a public entry point.

`libfw_support.a` is the console closure: `log.c` needs uLog and the printf
component, and neither was archived before.  `libfw_usbhost.a` is the same
completion for the wireless path: CherryUSB and the RK3568 cherryusb port used
to be image-only objects.  Without these two, a link that touched the console
or the USB path failed with symbols that existed in the image.

## 5. Linking

Always use the metadata, not a hand-written list:

```
export PKG_CONFIG_PATH="$PWD/dist/<profile>"
cc $(pkg-config --cflags freewificam) -c consumer.c
cc consumer.o $(pkg-config --static --libs freewificam) -o consumer
```

`--static` is required because the libc/gcc/`nosys` and `--gc-sections` items
are in `Libs.private`; without it pkg-config prints only the archive group.
On this SDK, `--libs` resolves to:

```
-L<dist>/<profile>/lib
-Wl,--start-group -lfw_app ... -lfw_arch -Wl,--end-group
-Wl,--gc-sections -lc -lgcc -lnosys
```

The group is not cosmetic.  The archives are mutually dependent at least in
one cycle: `libfw_board` needs `log_emit` from `libfw_osal_cmsis2`, which needs
the board UART and the generic timer, and the console closure reaches back into
`libfw_support`.  A one-pass order is fragile; `--start-group`/`--end-group`
makes the naming order in `.pc` irrelevant.  `--gc-sections` is required to
drop the printf formatting paths the archive member does not need (and the
`_out_char` hook that no one provides).

`-lc -lgcc -lnosys` describe the bare-metal AArch64 image this SDK is built
from (newlib, no OS).  A consumer running on a hosted OS uses its own C library
and startup; it should not blindly keep `-nostdlib`/`-lnosys`.  The archive
objects themselves only require a C library, `libgcc` and the OS calls listed
below.

### What a consumer still has to provide

A **log/configuration consumer** needs nothing beyond `pkg-config` and a
linker entry point.  The measured check links `-nostdlib -static -Wl,-e,main`
and ends with an empty `nm -u`.

A **Wi-Fi control consumer** has two extra requirements:

1. the profile's link-forcing symbols.  Static archives only pull what is
   referenced, and the driver adapter, firmware registration and USB bring-up
   are referenced only by the image's `-Wl,-u` set.  The set is generated into
   `dist/<profile>/freewificam.registration`, one flag per line; pass it to the
   consumer link verbatim.  For `rtl8xxxu-8723bu-netio-ar-ht20-ba` it is:

   ```
   -Wl,-u,usb_board_init
   -Wl,-u,lkpi_usb_init
   -Wl,-u,lkpi_usb_cherryusb_init
   -Wl,-u,lkpi_fw_rtl8723bu_register
   -Wl,-u,wlan_seam_adapter_init
   ```

   Without these flags the link can still succeed, but no adapter registers and
   `wlan_port_up()` fails at run time.  The file comes from the same fragment
   variables the image link uses, so it cannot drift from the build.  The
   backend's own `-Wl,-u,osCriticalEnter/osCriticalExit` are deliberately not
   listed: they belong to the image's OS glue, which is the consumer's side.

2. the OS and the linker-script symbols.  With the flags above, the measured
   unresolved set is:

   * 25 CMSIS-RTOS2 calls (`osThread*`, `osMutex*`, `osSemaphore*`,
     `osMessageQueue*`, `osTimer*`, `osDelay`, `osKernelGetTick*`,
     `osMemory*`, `osCritical*`).  These are the consumer's OS adapter.
   * `__noncacheable_start` and `__noncacheable_end`.  These come from the
     bare-metal linker script used by the image; a consumer supplies them from
     its own linker script or replaces the cache path.

   There is no undefined symbol from the driver, the net80211 stack, linuxkpi
   or CherryUSB once `libfw_usbhost.a` is in the group.  That is the closure
   this checkpoint measured; it is not a promise that every profile's optional
   path is covered.

`nm -u` policy for the gate: the log consumer must have an empty list.  The
SDK-wide gate is stronger than a per-consumer link: `sdk-check` takes the union
of every archive's imports, subtracts every archive's definitions, and fails on
anything outside `tests/host/sdk-consumer/undefined-allowlist.txt` (the
consumer-side families: CMSIS-RTOS2, the C library, lwIP and its `ethnetif`
glue, and linker-script symbols).  Any other name is a packaging defect, not a
consumer responsibility.

## 6. The consumer gate

`tests/host/sdk-consumer/` contains a small consumer and `check.sh`.  The
consumer includes only `fw_config.h` and `fw_log.h`, asserts the generated
identity at compile time and calls `LOG_ANCHOR()` plus `log_level_get()`.  The
script:

1. compiles each of the seven public headers alone, host compiler, with only
   `-I dist/<profile>/include`;
2. greps the published include tree for local development paths;
3. reads `pkg-config --cflags` and `--static --libs` and asserts the include
   path, the library path, the absence of `-D`, and the absence of internal
   source paths;
4. cross-compiles with the pkg-config cflags only, links with the pkg-config
   libs only, and verifies AArch64, the presence of `log_level_get`, an empty
   `nm -u`, and that the manifest names the requested profile.

Measured on 2026-11-03 in a fresh worktree at the M5 branch tip (no
consumer-added `-D`, no internal include path):

| Profile | Libraries | Gate |
|---|---|---|
| `net-netio-raw-tx256` | 5 | PASS |
| `rtl8xxxu-8723bu-netio-ar-ht20-ba` | 9 | PASS |

The image hash is not quoted here: the version banner embeds the commit id and
the dirty flag, so the same source legitimately hashes differently in another
commit.  The gate is the claim; the image is the profile's normal
`make.rtos image` output.

The gate proves configuration, compilation and link closure.  It does **not**
run the executable: the archives target bare-metal AArch64 and M5 is a build
checkpoint.  Runtime behaviour of the log path is the same object the board
image already boots, but it is not re-proven here.

To reproduce the unresolved Wi-Fi set, compile a three-call probe (for example
`wlan_port_get_hwaddr()`, `wlan_port_up()`, `wlan_port_xmit()`) with the
pkg-config cflags, then link it with the pkg-config libs plus the `-Wl,-u` set
from §5.  Without the `-Wl,-u` set the only unresolved names are six CMSIS2
calls; with it, the 27 names above appear.  The difference is the driver
adapter and USB path being pulled in.

## 7. Working, and aspirational

Working and measured in this checkpoint:

* seven self-contained public headers, installed with the exact dependency
  headers they forward;
* the generated configuration reaching the consumer, with compile-time identity
  checks and the no-`-D` rule enforced by the gate;
* a working `.pc`: include path, library path, archive group, private link
  items, version from `VERSION`, per-profile archive selection;
* a log consumer that configures, compiles, links and resolves completely on
  both a wired and a wireless profile;
* archives covering every object group of those two images, with a manifest
  (`profile`, archive, member count, size, undefined) that matches the SDK;
* a forced Wi-Fi link whose only remaining symbols are the consumer's OS and
  linker script.

Aspirational or explicitly out of scope:

* running the linked consumer on a board or under an OS: no M5 boot evidence;
* a Wi-Fi consumer that works without hand-adding the profile's `-Wl,-u` set:
  the `.pc` does not carry it;
* scan-with-SSID, a named join, AP/IBSS start, channel/HT configuration;
* a public bus transfer API and a public lifecycle (`fw_init`/`fw_run`);
* a netif registration API, zero-copy RX, TX completion reporting;
* cross-profile or long-term binary ABI stability; the manifest has no
  version/config-hash field yet;
* Zephyr `native_sim`, Zephyr on RK3568 and RT-Thread Smart (L2–L4).
  (Zephyr on RK3568 landed 2026-11-05 as an image, with a third kernel
  backend — see §8 L3.  It is not an SDK-consumer story: the module hosts the
  image's `main`, not a linked consumer.)

## 8. What L2–L5 require

The level names come from the module-build plan.  L1 is this document; the
others are not started.

**L1 (done, with the gaps above).**  The 2026-11-03 verification round closed
two of the four remaining items:

* the per-profile `-Wl,-u` registration set is exported as
  `dist/<profile>/freewificam.registration` (generated from the same fragment
  variables the image link uses);
* `sdk-check` now gates every archive: union of imports minus union of
  definitions against `tests/host/sdk-consumer/undefined-allowlist.txt`.  The
  gate found and fixed two packaging gaps (`fsl_sdmmc` objects and the lwIP
  port glue were referenced but not shipped in the first cut).

Still open: define the linker-script symbol contract (`__noncacheable_*`,
`__heap_*`, `__board_tests_*`, cache maintenance) for non-bare-metal
consumers; add a hosted-OS libc variant if the SDK is to be used with a normal
C runtime.

**L2 (native simulation).**  Zephyr `native_sim` with its CMSIS-RTOS2
implementation, a public `fw_init()`/`fw_port` descriptor, firmware registry and
register paths replayed through a bus stub (recorded URB sequence, no RF), and a
run that reaches `fw_init`.  This needs the lifecycle and bus shapes that
`fw_core.h`/`fw_bus.h` currently refuse to invent, plus an audit of section
registration (`BOARD_TESTS`/`FSymTab`) and `--whole-archive` behaviour in a
non-bare-metal link.

Probe result (2026-11-03): the Zephyr tree in this project configures, builds
and runs `native_sim` with the host toolchain and no west/SDK
(`cmake -DBOARD=native_sim -DZEPHYR_TOOLCHAIN_VARIANT=host`, hello_world runs).
So the simulator half is available.  The SDK half is not: today's archives are
AArch64 bare-metal objects, so L2 additionally needs a host/native build variant
of the library set and the `fw_init()`/`fw_bus` lifecycle.  That is a milestone,
not a verification gap.

**L3 (Zephyr on RK3568).**  The same `fw_init` plus scan/join state machine in a
Zephyr image on the board, still with the bus stub.  Zephyr's RK3568 USB host
port is not part of this; the data plane waits for a separate USB-host round.

Status (2026-11-05): reached, and past the bus stub.  The image is built by the
`freewificam` Zephyr module (`zephyr/module.yml` in the Zephyr tree, which
compiles this tree in place — no copy, no sparse checkout) on top of a third
kernel backend, `tests/board/rtos/platform/os/zephyr/`: the CMSIS-RTOS2 names
are served directly by Zephyr primitives rather than by
`subsys/portability/cmsis_rtos_v2`, so nothing above the OSABI changed —
linuxkpi, net80211, the rtl8xxxu driver and CherryUSB are the board-proven
bytes.  CherryUSB runs on this tree's own CMSIS-RTOS2 osal
(`third-party/cherryusb/ports/osal/cmsis2/`), not on CherryUSB's Zephyr osal:
that one services its hub timer from a Zephyr timer callback, where a
transfer wait cannot suspend.  Boot evidence on the RK3568 bench: an RTL8188EU
enumerates (3900 ms), scans 31 BSSes, joins the AR9271 access point
(`state=4`), answers ARP and 20/20 pings, `cases=5/5 backend=zephyr`.
Throughput on this backend has not been measured.

**L4 (RT-Thread Smart on RK3568).**  The same image-level state machine with an
RT-Thread CMSIS-RTOS2 shim.  RT-Thread's RK3568 EHCI host is likewise a separate
data-plane task; L4 acceptance stops at the bus stub.

**L5 (regression).**  The existing FreeRTOS and ThreadX images must be unchanged
in behaviour: the same boot/scan/join/data cases, per-profile image hashes and
the two-boot acceptance used by the module-build rounds, plus this SDK gate in
CI so packaging cannot drift from the image.

Measured (2026-11-03): FreeRTOS A2 2/2 and A6 shell pass on the merged tree;
the SDK gate now runs at the end of every `*_ci` target.  The ThreadX path had
never booted: the link passed `--defsym=HEAP_SIZE=<MiB>` without the `M`
suffix, so the tlsf window was 16 bytes, `osThreadNew()` failed and the image
reset.  Fixed to `<n>M`; the ThreadX 8851BU image now boots 2/2 with
`rtw89` PASS.  Two ThreadX findings stay open and are not caused by the build
refactor: the tick ladder reports 41 ms after the `rtw89` case (a tick-only
image passes at 11 ms), and the scan/join/dataplane cases are ordered before
the bring-up case by the `BOARD_TESTS` link order, so they report
`not attached`.  Neither FreeRTOS nor the image build changed.

Until those exist, "the SDK links" means exactly what §6 measures: a log and
configuration consumer, and a Wi-Fi control link that still needs the
consumer's OS, linker script and registration set.
