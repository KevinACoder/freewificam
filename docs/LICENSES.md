# LICENSES

Per-component license registry. This is not legal advice.

| Part | License | Notes |
|---|---|---|
| `tests/board/rtos/` (platform, app, build) | BSD-3-Clause | first-party; `LICENSE` at the repository root |
| `components/liblinux80211/` (written here) | BSD-3-Clause | the component's own code (`Kconfig`, `sources.mk`, seams, adapters, cases) |
| `components/liblinux80211/include/` + the imported bodies | BSD-3-Clause | the previous tree's first-party linuxkpi, byte-identical; the Linux face is the interface the stack compiles against, not the stack |
| `tools/` (written here) | BSD-3-Clause | |
| `tools/kconfig/` | ISC | Kconfiglib, vendored (see its `VENDOR`) |
| `third-party/FreeRTOS-Kernel/` | MIT | `LICENSE.md` at the tree root |
| `third-party/CMSIS_6/` | Apache-2.0 | `LICENSE` at the tree root |
| `third-party/CMSIS-FreeRTOS/` | Apache-2.0 AND MIT | `LICENSE` at the tree root |
| `third-party/lwip/` | BSD-3-Clause style | `COPYING` at the tree root |
| `third-party/cherryusb/` | Apache-2.0 | `LICENSE` at the tree root; eight patches applied in the tree, recorded in `patches/` |
| `third-party/fsl_sdmmc/` | BSD-3-Clause | NXP copyright blocks; `COPYING-BSD-3` beside the tree |
| `third-party/printf/` | MIT | |
| `third-party/tlsf/` | BSD-3-Clause | terms in the `tlsf.h` header |
| `third-party/ulog/` | MIT | |

## Combined products

The images this repository builds **contain no GPL unit** in this round: the
four acceptance images are platform, bus and network code under the licenses
above. There is therefore no GPL obligation attached to them today.

When `components/liblinux80211/` lands the real `net/mac80211` and
`net/wireless` (`GPL-2.0-only`), every binary that links it becomes a GPL-2.0
combined work regardless of the license of the parts written here. That
boundary is the reason the component is a separate build unit and a separate
profile family: the Realtek BSD-option profiles must never link it.

## Import direction

Imports come in; nothing goes back out to a tree that must stay free of
GPL-2.0. If a component here is ever useful elsewhere, the version to share is
the one without the imported stack linked in.
