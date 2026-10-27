# Changelog

All notable changes to this project are recorded here. The format follows the
spirit of Keep a Changelog; versions are the project's own.

## 0.0.2 - 2026-10-27

The first release cut from this repository. It is a **structural** release as
much as a feature release: the code moved to a per-component shape
(source + `ports/` + `patches/`), the host mock tier and the build outputs
were left out, and the release documents were written for a reader who has
never seen the development machine.

### Added

- **USB camera (UVC) line.** An isochronous transfer engine for the EHCI host
  port, the host video class's running-stream half, an application half and
  four board cases (bring-up and identity, the negotiated open, raw
  isochronous transfers, a bounded stream window). One profile (`uvc`) and one
  image per kernel; the isochronous engine is compiled only into that image.
  Measured on the reference board: 236-266 frames per 20 s window
  (11.8-13.3 fps, 1.85-2.10 Mbit/s), 2/2 clean boots per kernel.
- **The USB host stack** (CherryUSB with the board port): xHCI and EHCI roots,
  the hub, mass storage, and the EHCI control-IN coherency and qTD fixes.
- **The wireless lines**, each as a Linux kernel module arm and, where the
  line has one, as RTOS images: rtw89/RTL8851BU, rtl8xxxu/RTL8188EUS,
  rtl8xxxu/RTL8723BU, rtw88/RTL8821CU, iwlwifi/Intel AC 7260, and the vendor
  rtl8189fs stack for RTL8189FTV over SDIO.
- **Two RTOS kernels** behind one CMSIS-RTOS2 abstraction (ThreadX 6.5.1 and
  FreeRTOS 11.3.1), and the host conformance tier.
- **Release documents**: `VERSION`, `CHANGELOG.md` (this file), `LICENSE`,
  `LICENSES.md`, `docs/RELEASE.md`, `docs/RELEASE-NOTES-0.0.2.md`,
  `docs/MIGRATION-FROM-0.1.0.md` and `known_issues/`.

### Fixed

- **The wireless images' USB stack had no compile rules.** The stack's sources
  were linked into the rtl8xxxu images, but the build only emitted compile
  rules for the storage and 8851BU images. A clean tree could not build those
  images; an incremental build silently reused objects from whichever profile
  had run before.
- **Semaphore creation with interrupts masked.** The isochronous engine creates
  its per-slot semaphores inside the port's critical section, a context the
  FreeRTOS CMSIS-RTOS2 adapter refuses to create objects in; every camera
  submission failed with "no memory" on that kernel. The port now reserves a
  small pool of binary semaphores in thread context.
- **The release repository carried development-machine references**: home
  paths, a private build tree, measurement-record paths, internal round
  identifiers and a bench service name in a tool's environment interface.
  These are gone, and a gate (`tools/check-release-docs.sh`) now checks the
  reader-facing documents.

### Changed

- Layout: every imported tree is `source + ports/ + patches/`, patches are
  applied in the tree, and `patches/` is the record. There is no build-time
  replay and no top-level `patches/` directory.
- The board round is one command per boot (`tools/rtos/board-round.py`), and
  the judge reads nothing but the captured serial log.
- The CHANGELOG's relation to the reference tree's 0.1.0: that tag describes a
  different tree. Here the numbering restarts (0.0.x) and 0.0.2 is the first
  release cut from this repository.

### Removed

- The host mock tier (`tests/host/**`, `tools/mock/**`) and the mock-driven
  tests. They do not test the code this repository ships.
- Build outputs, object files and map files from the tree.
- The board photographs from the reference tree (no redistribution grant is
  recorded for the vendor manual they come from).

## Earlier

No release tag exists in this repository before 0.0.2. The reference tree's
last tag was 0.1.0; it describes a different tree and the two numbers are not
comparable.
