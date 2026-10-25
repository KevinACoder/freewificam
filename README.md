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

RTOS build/entry:

```sh
CASES="tick cmsis2 net emmc" tools/rtos/build.sh
```

The `wlan` case additionally needs the external vendor tree:

```sh
CASES="tick cmsis2 wlan" VENDOR_8189FS=<rtl8189fs at 13bbdbc> \
  HEAP_SIZE=16M tools/rtos/build.sh
```

The L1 probe is not part of this repository; it stays in the workspace.
