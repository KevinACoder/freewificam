# 04 - Radio

What the host can know about the radio side of this part, and where that
knowledge stops.  Most of this chapter is about the *border*: which numbers are
read out of the part, which are declared by the driver, and which do not exist
anywhere in this tree.

## What the host reads about the radio

Two identifiers arrive at attach, and neither one is a radio specification.

### `CSR_HW_RF_ID`

The register is read once at probe (`pcie/gen1_2/trans.c:4280`) and printed
(`pcie/gen1_2/trans.c:4293-4295`).  Its fields are decoded by five macros
(`iwl-csr.h:313-318`).  On the bench card the value is `0xd55555d5`, which
decodes as:

| Field | Mask | Value |
|---|---|---|
| type | bits 23:12 | `0x555` |
| step | bits 11:8 | `0x5` |
| dash | bits 7:4 | `0xd` |
| flavor | bits 3:0 | `0x5` |
| CDB | bit 28 | 1 |
| jacket | bit 29 | 0 |

Two facts follow, and neither is a conclusion about the silicon:

- `0x555` is **not one of the named RF types** in this tree.  The named values
  are `0x105`, `0x108`, `0x10a`, `0x10c`, `0x10d`, `0x112`, `0x113` and
  `0x114` (`iwl-config.h:485-492`).
- It does not matter for device selection: the 7260 entries in the
  device-info table do not set the RF-type match flag, so this field is not
  compared (`pcie/drv.c:1128` is the comparison that is skipped).
- **What the value means on this part is not established.**  The driver has a
  comment about a blank-OTP fallback, but it is gated on family 9000 and
  later (`pcie/gen1_2/trans.c:4282-4289`), so it does not apply here and
  cannot be borrowed to explain the value.

### CRF, CNV and WFPM identifiers

These are read by a helper that the same probe calls
(`pcie/gen1_2/trans.c:4042-4115`), and printed once
(`pcie/gen1_2/trans.c:4114-4115`).  On the bench card all three are zero:

```
Detected crf-id 0x0, cnv-id 0x0 wfpm id 0x0
```

The helper reads the identifiers from PRPH and, on family 9000 and later, uses
them to reconstruct an RF type when the strap reads zero
(`pcie/gen1_2/trans.c:4287-4291`).  On this family they are read and printed
and nothing else: the reconstructed-type path is not taken.

## Antennas and chains

The driver does receive antenna information, but it receives it from the NVM,
not from the PCI interface:

| Item | Where it comes from | Source |
|---|---|---|
| Valid transmit antenna mask | NVM RF configuration word | `iwl-nvm-parse.c:1281-1294` |
| Valid receive antenna mask | same word | `iwl-nvm-parse.c:1281-1294` |
| Chain counts | intersection of the firmware's chain count with those masks | `iwl-nvm-parse.c:1457-1477` |
| WiFi-only antenna | configuration field `.non_shared_ant = ANT_A` | `cfg/7000.c:91-93` |
| Antenna bit names | `ANT_A` = bit 0, and two more bits | `iwl-config.h:97-103` |

So the antenna count of a given unit is a *runtime* property.  This handbook
does not have a measurement of it for the bench card, and the product name is
not evidence either: `2ac` in a table string is a label, as
[`01-identity.md`](01-identity.md) explains.

The HT capabilities in the configuration - `stbc = true` and 40 MHz declared
for both bands (`cfg/7000.c:131-133`) - are declarations, not measurements.
They tell you what the driver will advertise if the NVM and the firmware allow
it; they do not tell you that this module has two chains or that it ever ran a
40 MHz channel.  LDPC is the same story: the 7260 configuration does not set
`.ldpc`, while the 7265 configurations next to it do (`cfg/7000.c:202-213`),
so no LDPC claim is made for this variant.  (It did not, in this project: the one attempt to declare
40 MHz produced the assertion described in [`07-errata.md`](07-errata.md).)

## Bands, channels and width

The band decision is made from NVM values at attach:

| Decision | Input | Source |
|---|---|---|
| 2.4 GHz enabled | SKU bit | `iwl-nvm-parse.c:1548` |
| 5 GHz enabled | SKU bit | `iwl-nvm-parse.c:1549` |
| 11n enabled | SKU bit, then masked by the `disable_11n` module parameter | `iwl-nvm-parse.c:1550-1552` |
| 11ac enabled | 11n enabled *and* a SKU bit | `iwl-nvm-parse.c:1553-1554` |
| MIMO disabled | SKU bit | `iwl-nvm-parse.c:1555` |

The channel list itself is a fixed table in the driver, in the order the NVM
stores channel flags: 14 entries in 2.4 GHz and 25 in 5 GHz, 39 in total
(`iwl-nvm-parse.c:79-86`).  The NVM supplies per-channel flags for those
positions; the table is the mapping from position to channel number.

One 7000-family rule changes the width of 5 GHz channels, and it is worth
knowing about because it is an erratum expressed as code: for some Indonesia
SKUs the NVM regulatory data wrongly allows wide channels in 5 GHz, and the
firmware asserts if they are used, so the driver force-disables them based on
the SKU field of the subsystem ID (`iwl-nvm-parse.c:1419-1448`, called at
`iwl-nvm-parse.c:1595`).  The details are in
[`07-errata.md`](07-errata.md); the bench card's PCI subsystem ID has SKU field 8, which is not one of the
two trigger values (the code reads the NVM's copy of the same field, not the
PCI one).

## Temperature

This is the one place where "no per-variant configuration" and "no protection"
must not be confused.

- `iwl7260_cfg` has no `thermal_params` field (`cfg/7000.c:127-138`).
- Three subsystems get a variant that *does* have one, with a steeper
  backoff table: `0x4a70`, `0x4a6e` and `0x4a6c`
  (`pcie/drv.c:821-825`, table at `cfg/7000.c:63-83`).
- When the configuration has no thermal table, the op mode does **not**
  disable thermal handling: it falls back to a built-in default table
  (`mvm/tt.c:826-835`, table at `mvm/tt.c:463-483`).

The two tables differ only in the six backoff slopes:

| Field | Default table | High-temperature variant |
|---|---|---|
| critical-temperature entry / exit | 118 / 96 | 118 / 96 |
| dynamic SMPS entry / exit | 114 / 110 | 114 / 110 |
| TX protection entry / exit | 114 / 108 | 114 / 108 |
| backoff at 112 C | 200 | 300 |
| backoff at 113 C | 600 | 800 |
| backoff at 114 C | 1200 | 1500 |
| backoff at 115 C | 2000 | 3000 |
| backoff at 116 C | 4000 | 5000 |
| backoff at 117 C | 10000 | 10000 |

So the correct statement for this card is: **it runs the default table**, not
"it has no temperature configuration".  The thresholds are the same on both
tables; the high-temperature variant just reduces power harder on the way up.
Whether the part reports a temperature at all in a form the host can read, and
how the value is produced, is in the firmware's domain and is not established
here.  (`mvm/debugfs.c` exposes `nic_temp` and `set_nic_temperature` for the
debug path, which implies a reported temperature exists; that is a debug
surface, not a specification.)

## Output power

- The 7260 configuration sets no power backoff table.  The table is attached
  to the 7265, 7265D, 3165 and 3168 configurations
  (`cfg/7000.c:177-226`); the consumer returns zero when the pointer is null
  (`mvm/ops.c:748-754`).
- Regulatory limits do not come from the driver: they come from the NVM
  regulatory section and are handed to the regulatory core.  The driver
  carries no dBm constants for this part, so this handbook does not either.
- **Not established:** maximum output power, per-rate power, the power table
  contents, and any conducted measurement.  None of it was measured here, and
  the tree does not contain it.

## What this chapter cannot say

- **Antenna count for this module.**  The NVM carries it; this project has not
  recorded it for the bench card.
- **Which band this module is wired for.**  The SKU bits decide what is
  enabled; the module's physical wiring is a vendor fact.
- **Any RF performance figure.**  No EVM, sensitivity, phase noise, adjacent
  channel rejection, spectral mask or gain was measured, and no vendor number
  is available to quote.  Over-the-air captures show which rate was used, not
  how good the radio is.
- **What the `0x555` RF-type field means.**  Not established, and not needed
  for device selection.
- **How the reported temperature is produced.**  A debug surface exists; its
  source and accuracy are not documented in this tree.
- **The calibration results.**  The calibration section is read at attach and
  its contents are opaque to the host; see [`05-nvm-otp.md`](05-nvm-otp.md).
