# freewificam - dev_v2

Final target: the **L2 linux-ko line** - the rtw89 driver together with the
net80211 stack inside one Linux kernel module - then the L3 RTOS re-host.  This
is the same shape as the previous `dev` line's kernelmod work.

This is the public release repository.  It contains the final L2/L3 code and
the tests, tools and docs that belong to it.  L1 measurement probes, the
firmware TRM, specs, issues and run evidence live in the workspace and never
land here.

The two lines have the same shape: **source + ports + patches** per imported
component, and one build/entry directory per line.  Patches are applied in the
imported tree by default; `patches/` is the record (patch file, README,
manifest), not a build-time replay (see `docs/PATCH-POLICY.md`).

Layout:

```
components/                     first-party components
  linuxkpi/                     Linux compatibility layer: fw registry + RTOS runtime
  net80211/                     imported net80211 core: source + ports/ + patches/
wireless-drivers/               pinned wireless drivers (source + ports/ + patches/)
  iwlwifi/ rtl8xxxu/ rtw88/ rtw89/
  rtl8189fs/                    external vendor pin: provenance + ports/ + patches/ record
third-party/                    RTOS ported components only (source + ports/ + patches/)
  CMSIS_6/ CMSIS-FreeRTOS/ FreeRTOS-Kernel/ threadx/
  lwip/ fsl_sdmmc/ printf/ tlsf/ ulog/
tests/board/
  linux-ko/                     linux-ko line: build/entry + ports/
  rtos/                         RTOS line: app/, build/, platform/, tests/
tools/                          gates and helpers
docs/                           commit convention and patch policy
```

Rules:

- Do not merge or rebase the previous `dev` line. Take a file explicitly with
  `git show <sha>:<path>` and record the source commit in the importing commit
  message.
- Each commit builds; documentation and tooling commits prove it with the
  commit-message checker.
- ASCII in the repository; the workspace documents may be Chinese.
- No local paths, lab identifiers, knowledge-base references or run evidence.
- Commit format: `docs/COMMIT-CONVENTION.md`; gate: `tools/check-commit-msg.sh`.

Build (linux-ko, firmware file mode, no blob).  `DRIVER` selects the one driver
the module carries; `rtw89` is the default, `rtw88`, `rtl8xxxu` and `iwlwifi`
are the other byte-faithful arms.  The 8188 arm keeps `request_firmware()` and
reads `/lib/firmware/rtlwifi/rtl8188eufw.bin` from the image.

```sh
BUILD=/path/to/linux-ko KSRC=/path/to/kernel/src KOBJ=/path/to/kernel/obj \
  KREL=7.3.0-rc2 DRIVER=rtw89 FW_EMBED=0 tests/board/linux-ko/build.sh
```

RTOS build/entry: the image is a function of one profile file, not of the
command line.  `tests/board/rtos/config.mk` names the active profile from
`tests/board/rtos/configs/`; `make -C tests/board/rtos/build image
BACKEND=<freertos|threadx>` builds exactly that image, and `tools/rtos/build.sh
images` builds both backends of it.  The profiles carried today:

| profile | what the image carries |
|---|---|
| `wlan-delivery`, `wlan-obs`, `wlan-ping` | the 8189FTV vendor stack over SDIO |
| `pcie-nvme`, `pcie-bench`, `pcie-7260` | the PCIe root complex, NVMe and the AC 7260 endpoint (B0-B2 gates) |
| `iwlwifi-7260` | plus the iwlwifi/mvm driver over the linuxkpi PCI engine (attach + ALIVE) |
| `iwlwifi-7260-sjd` | plus the ladder cases: scan, open join, raw-L2 dataplane and the lwIP netif with ping/iperf |

The 8189FTV profile additionally needs the external vendor tree:

```sh
VENDOR_8189FS=<rtl8189fs at 13bbdbc> tools/rtos/build.sh
```

The board round that drives these images (TFTP staging, boot, serial capture,
peer iperf, offline judge) is `tools/rtos/board-round.py`; the judge is
`tools/rtos/judge.py` and reads nothing but the captured log.

The L1 probe is not part of this repository; it stays in the workspace.
