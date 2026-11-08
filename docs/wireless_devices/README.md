# Wireless device handbooks

This directory describes the wireless chips this project drives, one handbook
per module.  A handbook answers a question the driver source cannot answer by
itself: what is this part, how is it wired to the host, what has to happen to
it before it will run, and which of those facts are established rather than
assumed.

The driver is the authority on what the driver does.  It is not a datasheet:
it records the sequences and constants one vendor needed, not the silicon
they belong to.  A handbook therefore has three jobs:

- say what the part is, at the level the driver and the firmware image expose;
- say what the host must do to it, in the order the driver does it;
- say plainly where the knowledge stops.

## How to read a handbook

Every table row carries a source and one of three confidence labels.  The
labels are the whole point of the exercise: a claim is only as good as its
label, and a reader must be able to tell them apart without guessing.

| Label | Means | How to check it |
|---|---|---|
| `Measured here` | observed on the bench card with the driver in this tree | the command and its output are quoted in `08-observing.md`; nothing private is referenced |
| `Driver source` | the contract the vendored driver encodes | the cited `file:line` exists in `wireless-drivers/`; the line says what the row says |
| `Not established` | no public source and no measurement | nothing to check: the handbook says so instead of filling the gap |

Three rules follow from the table:

- A claim that is not `Measured here` and not `Driver source` is not made.
  There is no fourth label, and prose does not get a waiver.
- A capability *declared* by the driver is not a capability *demonstrated* by
  the part.  When the driver fills in a capability structure, the row says so.
- Sibling parts are not evidence.  The 7260, 7265, 3160 and 8260 share driver
  code; they do not share a datasheet.  A number taken from one of them is not
  written down for another.

## Citations

- A driver citation is relative to `wireless-drivers/iwlwifi/`, so
  `pcie/drv.c:804` means `wireless-drivers/iwlwifi/pcie/drv.c:804`.
- Everything outside that tree is relative to the repository root, for example
  `docs/boards/rk3568-itx.md:91` or `firmware/iwlwifi/iwlwifi-7260-17.ucode`.
- Line numbers belong to the pinned import recorded in
  `wireless-drivers/iwlwifi.provenance.md`.  If that tree is re-imported, the
  numbers move and the citations have to be re-checked.
- `tools/check-device-docs.py` resolves every citation and every relative
  link; `--dump` prints the cited line next to the citation.
- `tools/check-release-docs.sh` is the release gate for reader-facing text:
  ASCII only, no private repository names, no internal round identifiers, no
  records that live outside this repository.  Both checks are run before a
  handbook commits.

## Modules

| Module | Bus | Form | Handbook |
|---|---|---|---|
| Intel Wireless-AC 7260 (`iwl7260`) | PCIe | SoftMAC; `iwlmvm`, firmware TLV container | [`ac7260/README.md`](ac7260/README.md) |
| Realtek RTL8851BU (`rtw89`) | USB | SoftMAC; `rtw89` | not written |
| Realtek RTL8821CU (`rtw88`) | USB | SoftMAC; `rtw88` | not written |
| Realtek RTL8188EU (`rtl8xxxu`) | USB | SoftMAC; `rtl8xxxu` | not written |
| Realtek RTL8189FTV | SDIO | vendor FullMAC | not written |
| Realtek RTL8723BU (`rtl8xxxu`) | USB | SoftMAC; `rtl8xxxu` | not written |

Each handbook's identity chapter carries the IDs, subsystem IDs and firmware
names it can prove.  The index deliberately does not repeat them: a list here
would become a second, unverifiable source.

## Adding a module

1. Copy `ac7260/` and keep the eight chapter filenames; a reader who learns one
   handbook should not have to learn a second layout.
2. Keep the rule that a chapter ends with *What this chapter cannot say*.
3. Add the row above, and only the row: an empty directory with a placeholder
   README is noise in a tree that is meant to be read.
4. Run `tools/check-device-docs.py` and `tools/check-release-docs.sh`.

## What this directory is not

- It is not a datasheet.  No vendor datasheet for these parts is available to
  this project, and none is reconstructed here.
- It is not a driver porting guide.  The port lives in `tests/board/` and the
  driver's own `ports/` directory; this directory describes the part.
- It is not a firmware command reference.  The command and notification
  layouts are defined by the driver headers under
  `wireless-drivers/iwlwifi/fw/api/`, which are the authority.
- It carries no measurement records.  The records stay outside this
  repository; a handbook states what was measured and how to repeat it.
