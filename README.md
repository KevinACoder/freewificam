# freewificam - dev_v2

Final target: the **L2 linuxko line** - the rtw89 driver together with the
net80211 stack inside one Linux kernel module, then the L3 RTOS re-host.  This
is the same shape as the previous `dev` line's kernelmod work
(`components/net80211/` + `ports/linux/` + `ports/net80211/`).

This is the public release repository.  It contains the final L2/L3 code and
the tests, tools and docs that belong to it.  L1 measurement probes, the
firmware TRM, specs, issues and run evidence live in the workspace and never
land here.

The framework is imported from `dev@31adcfe`:
`components/net80211/`, `components/linuxkpi/`, `ports/linux/`,
`ports/net80211/`, `tests/board/linux-ko/`, together with the verbatim
`third-party/rtw89/`, `third-party/rtw88/` and `third-party/rtl8xxxu/` trees.
The firmware blobs are deliberately absent; an image supplies them at
`/lib/firmware/` (`rtw89/rtw8851b_fw-1.bin`, `rtw88/rtw8821c_fw.bin`,
`rtlwifi/rtl8188eufw.bin`).

Rules:

- Do not merge or rebase the previous `dev` line. Take a file explicitly with
  `git show <sha>:<path>` and record the source commit in the importing commit
  message.
- Each commit builds; documentation and tooling commits prove it with the
  commit-message checker.
- ASCII in the repository; the workspace documents may be Chinese.
- No local paths, lab identifiers, knowledge-base references or run evidence.
- Commit format: `docs/COMMIT-CONVENTION.md`; gate: `tools/check-commit-msg.sh`.

Layout:

```
components/net80211/ the imported net80211 stack
components/linuxkpi/ firmware registry (no blobs)
ports/linux/         Linux port, mac80211 substitute and the driver glue
ports/net80211/      the net80211 port and bridge
third-party/rtw89/   verbatim rtw89 import (provenance beside it)
third-party/rtw88/   verbatim rtw88 import
third-party/rtl8xxxu/ verbatim rtl8xxxu import (provenance beside it)
patches/rtl8xxxu/    the byte manifest of the rtl8xxxu import
tests/board/linux-ko/ the kernelmod build and entry
tools/               gates and helpers
docs/                commit convention
```

Build (firmware file mode, no blob).  `DRIVER` selects the one driver the
module carries; `rtw88` and `rtl8xxxu` are the other byte-faithful arms.  The
8188 arm keeps `request_firmware()` and reads
`/lib/firmware/rtlwifi/rtl8188eufw.bin` from the image.

```sh
BUILD=/path/to/linux-ko KSRC=/path/to/kernel/src KOBJ=/path/to/kernel/obj \
  KREL=7.3.0-rc2 DRIVER=rtw89 FW_EMBED=0 tests/board/linux-ko/build.sh

BUILD=/path/to/linux-ko KSRC=/path/to/kernel/src KOBJ=/path/to/kernel/obj \
  KREL=7.3.0-rc2 DRIVER=rtl8xxxu tests/board/linux-ko/build.sh
```

The L1 probe is not part of this repository; it stays in the workspace.
