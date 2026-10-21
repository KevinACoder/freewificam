# Import information

All files under `net80211/` and `driver/` are verbatim copies (byte
identical, verified with cmp) from the NetBSD netbsd-11 sources.

- Source repository: https://github.com/KevinACoder/netbsd-src (fork of
  the NetBSD CVS tree, branch netbsd-11)
- Tree revision at import: `dea823526f92de8fb73b1a56b89f44b60a0f1637`
  (2026-09-07)
- Last per-directory commits at import time:
  - `sys/net80211/`: `95093fc595ba1f882f72b370c4abc23753ac8035`
  - `sys/dev/usb/if_urtwn.c`: `9a78d88900a4793bd333a709b420c85050dd09df`

## Mapping

| Import target | Upstream path |
|---|---|
| `net80211/*.c`, `net80211/*.h` | `sys/net80211/*` |
| `driver/urtwn/if_urtwn.c` | `sys/dev/usb/if_urtwn.c` |
| `driver/urtwn/if_urtwnreg.h` | `sys/dev/usb/if_urtwnreg.h` |
| `driver/urtwn/if_urtwnvar.h` | `sys/dev/usb/if_urtwnvar.h` |
| `driver/urtwn/rtwnreg.h` | `sys/dev/ic/rtwnreg.h` |
| `driver/urtwn/rtwn_data.h` | `sys/dev/ic/rtwn_data.h` |
| `firmware/urtwn/rtl8188eufw.bin` | `external/realtek/urtwn/dist/rtl8188eufw.bin` |

The upstream `sys/net80211/CHANGES`, `Makefile` and `files.net80211`
are not imported: they belong to the NetBSD build system.

## Rules for changes

- Imported files stay byte-identical whenever possible. The compiler is
  pointed at `compat/netbsd/` shadow headers through the include path
  (and, where unavoidable, a forced include of `port/port_config.h`)
  instead of editing the sources.
- If an import file must be touched, keep the edit minimal, mark it
  with a `/* NET80211_PORT(L): ... */` comment, and record it here.
