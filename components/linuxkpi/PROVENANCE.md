# components/linuxkpi - provenance

| Item | Value |
|---|---|
| Upstream | first-party code of this project's previous line (`freewebcam` repository, branch `feat/lkpi-stabilize-r1`, tip `f8e33c3fabcd1d1417068511e547b8996df2b51c`); the files are clean-room first-party except `skbuff/skbuff_pool.h` (see License) |
| Pin | per-file blob IDs below, taken from the committed blobs of that branch with `git show <branch>:<path>`; the branch's working tree has staged edits on four of the files, so no working-tree content was read |
| License | BSD-2-Clause for the eight code files; `skbuff/skbuff_pool.h` carries no SPDX line and self-describes an Apache-2.0 lineage from the TizenRT/Realtek osdep skbuff pool, whose verbatim import now lives in `components/skbuff/` |
| Vendoring | vendored verbatim from the committed blobs (blob IDs below); not a git submodule (user ruling 2026-10-07: a component, not third-party) |
| Fetched | 2026-10-07 |
| Local differences | the engine-adoption round (linuxkpi-usb-test R1) changed eight files/call sites (`usb/linuxkpi_usb.c`, `usb/linuxkpi_usb.h`); every change is registered in `DIVERGENCES.md`. The other seven files keep their import bytes; the manifest's blob IDs remain the import anchor |
| Compiled set | the five contract headers compile as the contract translation unit in the `contract-check` target of `tests/host/linuxkpi-usb-test/`; the USB engine compiles and runs in `tests/host/linuxkpi-usb-test/` behind the component's backend seam (adoption round R1; all changes in `DIVERGENCES.md`), with the host build supplying the core services of `linuxkpi_core.h` at the same path. Known warnings of the imported headers, recorded rather than fixed (zero patch): `sysfs.h` redefines `__stringify` after `linuxkpi_core.h`, and `linuxkpi_core.h:1754` passes a `struct mutex *` where `lkpi_spin_lock_irqsave` takes a `spinlock_t *` |
| Consumers | the linuxkpi-usb host mock suite (the `linuxkpi-usb-test` Gate 1 suite) consumes the engine through the backend seam; the contract headers anchor the linuxkpi API face for both host mock suites and for the later linuxkpi body |
| Reason | the host mock suites need the linuxkpi API face (and the USB engine) in-tree as a fixed object under test, so their assertions anchor on in-repo paths and blob IDs instead of re-creating headers or re-writing the engine per mock round |

## Import manifest

| File | Lines | Source blob (previous line, `feat/lkpi-stabilize-r1`) | License | Lineage |
|---|---|---|---|---|
| `include/linux/usb.h` | 571 | `1397ecf386b97aca22cb2c1df18980ea77da135b` | BSD-2 | first-party (Linux API shapes) |
| `include/linux/device.h` | 36 | `bba0cd4a4b953150198d852514a2f001c5209971` | BSD-2 | first-party |
| `include/linux/sysfs.h` | 69 | `7ca5fe9da5bd3a89499f622c31012654da393a35` | BSD-2 | first-party |
| `linuxkpi_core.h` | 2406 | `adf8cd5e9fc8b950fc27bfca623c83db7d40a6e2` | BSD-2 | first-party |
| `skbuff/skbuff_pool.h` | 138 | `0c7f54658433d08b50e6ec51502657ac5c0f550a` | Apache-2.0 lineage, no SPDX line | descended from the TizenRT/Realtek osdep skbuff pool (verbatim unit: `components/skbuff/`) |
| `usb/linuxkpi_usb.c` | 2241 | `c1f4f86dcdd557b78f7260c66ef9a514fa3aa031` | BSD-2 | semantic port of FreeBSD `sys/compat/linuxkpi/common/src/linux_usb.c` at `d5c09e32c` |
| `usb/lkpi_usbh_class.c` | 316 | `77c2f37728a7469de1017d361943f6d33e6c354c` | BSD-2 | first-party (class hooks) |
| `usb/linuxkpi_usb.h` | 83 | `aee1fe834428dbb43503de15c588cd17fbbaef51` | BSD-2 | first-party (usbkpi record ring) |
| `usb/usbkpi_cmds.c` | 360 | `38eb3fe3088c53715f760f3e37addb3d93ef2c7f` | BSD-2 | first-party (usbkpi diagnostics) |

The contract-header closure is self-contained with the relative layout preserved:
`include/linux/usb.h` includes `../../linuxkpi_core.h` and `<linux/device.h>`;
`device.h` includes `linuxkpi_core.h` and `<linux/sysfs.h>`; `sysfs.h` includes
`linuxkpi_core.h`; `linuxkpi_core.h` includes only C standard headers and
`skbuff/skbuff_pool.h`; `skbuff_pool.h` includes `<stdint.h>`.

## Not imported (and why)

* `linuxkpi_core.c` (1656 lines), `linuxkpi_80211.c` (1725), `linuxkpi_impl.h` (124),
  `linuxkpi_net80211.h` (80), `fw_registry.c` (63) - the reference-rewrite objects of
  the repository-bootstrap spec; their rewrite belongs to the later linuxkpi body and
  the wireless milestone.
* `skbuff/skbuff_pool.c` (580 lines, source blob `1c84b2ce7ddcf0370830f69769d8ffee72fa496f`)
  - the ported pool body is not the reference; the source of truth is the verbatim
  TizenRT unit in `components/skbuff/` (user ruling 2026-10-07). The API-face
  convergence between `skbuff/skbuff_pool.h` and that unit is open work (see
  `DIVERGENCES.md`).
* The remaining 51 `include/linux/` headers, 4 `include/net/` and 1 `include/asm/`
  header - no consumer in this round (they serve the Linux-family drivers later).
* `usb/tests/**` (GPL-2.0) - not moved into the permissively licensed line.
* `.gitkeep`, `driver-check/`, and the previous line's Chinese ledgers - not
  imported (English repository); their content is anchored by blob ID of the same
  branch: tree `PROVENANCE.md` `f48f2e5ee8da8b53a3e6d3683e36ce5a5eceffc2`,
  `skbuff/PROVENANCE.md` `eac399c2ea222f8297c0e9a010b8b1c82a448db7`,
  `skbuff/DIVERGENCES.md` `7618484a4cb110a52c3fbe842f14633be70c62f7`,
  `usb/PROVENANCE-USB.md` `a7e7925325f97e92cb16a87af7ae420f33ceb9f0`,
  `usb/DIVERGENCES-USB.md` `7fe5d592893e3c57e29d40bf028e1dd99e1a83f1`.

## Rules for changes

* These nine files stay byte-identical. Adaptations (for example dropping the
  `tx_api.h` dependency of the USB engine, placing `cmsis_os2_ext.h`, replacing the
  board timer header, or the skbuff header convergence) happen in the round that
  consumes them and are recorded in `DIVERGENCES.md` before they land.
* The imported blobs carry the previous line's comments, including Chinese ones in
  the USB engine and `linuxkpi_core.h`; the bytes stay as they are. Like the other
  imported units they are exempt from this repository's authoring scans (the
  local-knowledge and ASCII gates skip imported unit trees), and this file plus
  `DIVERGENCES.md` - both project-authored English - are their record. Translating
  the comments is a consumer-round change and would be a ledger entry, not a
  rewrite inside the import.
