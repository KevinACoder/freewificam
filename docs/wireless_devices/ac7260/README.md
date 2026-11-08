# Intel Wireless-AC 7260 - device handbook

Status: first edition, 2026-11-08.  Everything labelled `Measured here` was
taken on one card (subsystem ID `5070`) with the driver in this repository.
Everything labelled `Driver source` is what the vendored iwlwifi tree encodes.
Everything else says `Not established`, and that is a result too.

## In one page

| Item | Value | Confidence |
|---|---|---|
| Vendor and device | `8086:08b1`, class `0x028000` (network controller), PCIe endpoint | Measured here |
| Subsystem | `08b1:5070` on the bench card | Measured here |
| Config-space revision | `0x93` | Measured here |
| `CSR_HW_REV` (BAR0 `0x028`) | `0x144` | Measured here |
| Link | 2.5GT/s, one lane, in both the capability and the status registers | Measured here |
| BAR0 | 8 KiB, 64-bit, non-prefetchable | Measured here |
| Interrupt | one MSI vector; MSI-X is not used by this part | Measured here, Driver source |
| ASPM | L1 enabled at the link; the driver forces L0S off | Measured here, Driver source |
| Firmware | `iwlwifi-7260-17.ucode`, API 17, version string `17.bfb58538.0` | Measured here |
| Firmware digest | `5d81a6003df0228a497ad27f916ba2c979614b4c439b0f45a5f2873dc0607fe8`, 1,049,340 bytes | Measured here |
| Driver configuration | `iwl7260_cfg`, one of the three 7260 names, `iwlmvm` op mode | Driver source |
| NVM | 16 KiB low image, hardware section 0, version constant `0x0a1d` | Driver source |
| Temperature thresholds | none for this variant; only the high-temperature variants get a table | Driver source |
| Radio bands | both bands declared by the driver configuration; the NVM SKU decides at attach | Driver source |
| MAC address | read at attach, printed once; not a constant of the part | Measured here |

The two revision numbers are not the same number and must not be confused:
`0x93` is the PCI configuration-space revision field, `0x144` is the word the
driver reads from BAR0 offset `0x028`.  The driver prints the second one as
`rev=0x144`; a PCI tool prints the first one as `(rev 93)`.

## Which 7260 is this?

`0x08B1` is handled by three names in the driver's device table - `7260 2ac`,
`7260 2n` and `7260 n` - selected by subsystem ID.  The bench card's subsystem
ID `0x5070` has **no entry of its own**; it matches the wildcard entry for
device `0x08B1`, which carries the `2ac` name.  The full chain, including the
reverse scan that makes the wildcard the fallback rather than the first match,
is in [`01-identity.md`](01-identity.md).

Consequence for a reader: the string "Dual Band Wireless AC 7260" in the log
is the name attached to the wildcard entry, not proof that the driver
recognises this subsystem ID.

## Chapters

| Chapter | Answers |
|---|---|
| [`01-identity.md`](01-identity.md) | Which part is this, which variants does the driver know, which firmware pairs with it, where does the source come from |
| [`02-silicon.md`](02-silicon.md) | How is the chip brought out of reset: clocks, power block, reset handshakes, the driver-view block diagram |
| [`03-bus.md`](03-bus.md) | How the host talks to it: configuration space, BAR, interrupts, DMA, the two descriptor rings, doorbells |
| [`04-radio.md`](04-radio.md) | What the driver knows about the radio side, and what it does not: identifiers, antennas, bands, temperature, power |
| [`05-nvm-otp.md`](05-nvm-otp.md) | Where the per-unit data lives, how it is read, which sections are required, what the calibration section means to the host |
| [`06-firmware.md`](06-firmware.md) | The firmware container, the capability gates it sets, the command-version table, and the decoded section list of the image this tree ships |
| [`07-errata.md`](07-errata.md) | The workarounds the driver carries and the one firmware assertion this project has reproduced, with the inputs that trigger it |
| [`08-observing.md`](08-observing.md) | How to get evidence from this part: the commands, the anchors, the register windows, the tracing surfaces |

A reasonable reading order is 01, 08, 02, 03, then 04 to 06, then 07: identity
first, then the instruments, then the part.

## The five questions this page should answer

**What is it?**  A single-band-per-radio, two-stream-capable 802.11ac
SoftMAC part on PCIe, driven by `iwlmvm`; the host runs the 802.11 state
machine, the firmware runs the radio and the lower MAC.  The evidence for the
SoftMAC split is the driver's op-mode table: `iwlmvm` registers against
mac80211 and there is no vendor MLME in the firmware interface.

**How many variants does the driver know?**  Three names for `0x08B1`, a
high-temperature configuration for three subsystems, and a separate `0x08B2`
device ID.  See the table in `01-identity.md`; the list is 24 entries and is
meant to be read, not summarised.

**How does it connect?**  PCIe, one lane at 2.5GT/s, one MSI vector, BAR0 of
8 KiB.  The driver's DMA uses 36-bit addresses with a 32-bit fallback and
requires 4 KiB alignment for receive buffers.

**Which firmware?**  `iwlwifi-7260-17.ucode`, API 17 exactly; the container
and its capability bits are decoded in `06-firmware.md`, and the image is in
`firmware/iwlwifi/`.

**What is measured, what is declared, what is unknown?**  Measured: identity,
link, interrupt, memory window, firmware version, the assertion in `07`.
Declared: everything in the driver configuration tables and the NVM parsing
path.  Unknown, and written as unknown: the die's internal blocks, clock
frequencies, supply voltages, RF figures, and the calibration contents.

## What this handbook deliberately does not contain

- No radio figures.  No EVM, sensitivity, maximum output power or antenna
  gain was measured, and no vendor number is available to quote.
- No clock tree numbers.  The driver never programs a frequency, so nothing in
  the tree establishes one.
- No die block diagram.  The diagram in `02-silicon.md` is the driver's access
  surface, labelled as such, not a floor plan.
- No firmware internals.  The container is described; what the firmware does
  with it is not.
