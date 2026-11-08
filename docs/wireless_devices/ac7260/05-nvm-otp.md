# 05 - NVM and OTP

The per-unit data: where it lives, how the host reads it, which parts of it the
driver actually uses, and where the data that the host never interprets sits.
"Calibration happens in the firmware" is the one-sentence version; this
chapter is about what that sentence leaves out.

## What "NVM" means here

The driver calls the per-unit storage "NVM" and reads it through a host
command, not through a memory window.  There is no register the host can read
to get the MAC address; there is a command that returns one 2 KiB chunk at a
time.

| Item | Value | Source |
|---|---|---|
| Window size | 16 KiB (the low image) | `iwl-config.h:314`, `cfg/7000.c:53` |
| Hardware section number | 0 | `cfg/7000.c:60` |
| Format | the regular format, not the extended one | `iwl-config.h:67-71`; `iwl7260_cfg` sets no `nvm_type` |
| Access | `NVM_ACCESS_CMD` (id `0x88`) | `fw/api/commands.h:353` |
| Chunk size | 2 KiB | `mvm/nvm.c:18` |

The driver's own log lines say "OTP/NVM" interchangeably
(`mvm/nvm.c:230`), which is accurate: on this part the data is fused into the
device, but the host's view of it is a sectioned read, and that view is all
this chapter can describe.

## The read protocol

The request (`struct iwl_nvm_access_cmd`, `fw/api/nvm-reg.h:113-120`) is five
header fields, eight bytes, plus a payload: an op code, a target, a section type, a byte
offset within the section and a length.  The response
(`struct iwl_nvm_access_resp`, `fw/api/nvm-reg.h:130-136`) repeats the offset
and length, carries a status, and appends the data.

The driver's read path (`mvm/nvm.c:72-143`):

1. Build `NVM_ACCESS_CMD` with `op_code = NVM_READ_OPCODE`, the section, an
   offset and a 2 KiB length (`mvm/nvm.c:75-91`).
2. Send it with `CMD_WANT_SKB | CMD_SEND_IN_RFKILL` - the read is allowed in
   RF-kill, which matters for a port that boots with the radio off
   (`mvm/nvm.c:83-87`).
3. Check the status.  One status is special: a "not valid address" error at a
   non-zero offset means the section ended exactly on a 2 KiB boundary, and is
   treated as end-of-section rather than as a failure
   (`mvm/nvm.c:105-119`).
4. Verify that the response's offset equals the request's offset, then copy
   the returned bytes to `data + offset` (`mvm/nvm.c:129-137`).
5. Repeat while the device returned a full chunk (`mvm/nvm.c:190-207`).

`iwl_nvm_read_section()` (`mvm/nvm.c:179-214`) drives that loop for one
section; `iwl_nvm_init()` (`mvm/nvm.c:306-...`) runs it for sections 0 through
12 into a single 16 KiB buffer, skipping sections the device does not have -
a missing section is `-ENODATA` and is not a failure
(`mvm/nvm.c:323-330`).  A per-unit fixup hook runs after each section
(`mvm/nvm.c:340`, `mvm/nvm.c:209`).

Note the shape of the failure: reading the sections is forgiving (absent
sections are skipped), and the *parse* step is where the required ones are
enforced, in a different file.  A port that reports "NVM read failed" for a
missing optional section will be wrong about the cause.

## Sections

| ID | Name | Role in this driver |
|---|---|---|
| 0 | the hardware section (`nvm_hw_section_num = 0`) | required: the MAC address and the subsystem-ID copy live here |
| 1 | `SW` | required: the version word and the radio configuration words live here |
| 3 | `REGULATORY` | read; per-channel flags and regulatory data |
| 4 | `CALIBRATION` | required to be non-empty; contents not interpreted |
| 5 | `PRODUCTION` | read like any other section; not used by this path |
| 8 | `REGULATORY_SDP` | the 3168-series variant of the regulatory section |
| 11 | `MAC_OVERRIDE` | the newer-format MAC override; not used by this format |
| 12 | `PHY_SKU` | the newer-format SKU section; not used by this format |

IDs from `fw/api/nvm-reg.h:84-100`.  The mandatory set is decided by the
format, not by the family: for the regular format the driver requires `SW`
and the hardware section (`mvm/nvm.c:227-232`); the branches that require a
regulatory section, a MAC override or a PHY SKU section apply to the newer
formats (`mvm/nvm.c:233-260`).  The calibration section is required one file
later, in the parser (`iwl-nvm-parse.c:1559-1567`).

## What the driver takes out of it

| Output | Section | Source |
|---|---|---|
| MAC address | hardware | `iwl-nvm-parse.c:1391-1400` |
| NVM version | `SW` | `iwl-nvm-parse.c:1245-1251` |
| Radio configuration type, step, dash, flavor | `SW` | `iwl-nvm-parse.c:1281-1294` |
| Valid TX/RX antenna masks | radio configuration word | `iwl-nvm-parse.c:1281-1294` |
| Band, 11n, 11ac, MIMO capability bits | SKU word | `iwl-nvm-parse.c:1547-1555` |
| Per-channel flags | `REGULATORY` | `iwl-nvm-parse.c:1568-1600` |
| Calibration version marker | `CALIBRATION` | `iwl-nvm-parse.c:1600` |

The MAC address deserves a detail: on this family it is read from the
hardware section at offset `0x15` (`iwl-nvm-parse.c:32-33`) and the 16-bit
words are byte-swapped on the way out; the driver's comment states the
storage order as little-endian per 16-bit word (`iwl-nvm-parse.c:1394-1400`).  It does **not** come
from the PCI configuration space, and it does not come from the CSR strap
registers: that path exists (`iwl-nvm-parse.c:1312-1332`) but is selected by
a base parameter this family does not set (`iwl-nvm-parse.c:1389-1391`).  A
reader who looks for the MAC in `CSR_MAC_ADDR0_STRAP` on this part will find
something that the driver never uses.

The "OTP minor version" in the boot log is a separate register,
`REG_OTP_MINOR` at `0xa0333c` (`iwl-prph.h:539`), read only for the log line
(`iwl-nvm-parse.c:1411-1413`).  It was `0x0` on the bench card, which says
nothing about the data version - the NVM version is the word above.

## The version word

- The version is a field in the `SW` section (`iwl-nvm-parse.c:37`,
  `iwl-nvm-parse.c:1245-1251`).
- The configuration declares an expected value, `0x0a1d`
  (`cfg/7000.c:24`, `cfg/7000.c:134`).
- On this driver path, the value is parsed and printed
  (`mvm/nvm.c:394-395`) and **never compared** against the declared one.  The
  comparison exists only in the DVM op mode (`dvm/main.c:1219-1228`).
- So an NVM version mismatch on this card is a fact the driver prints and
  otherwise ignores.  Whether the firmware cares is not established.

## The calibration section

This is the part of the NVM that the host sees and cannot read.

- The section must be present and non-empty; if it is not, the parser fails
  (`iwl-nvm-parse.c:1559-1567`).
- Apart from that check, the only thing the parser does with calibration is
  set a marker field to 255 (`iwl-nvm-parse.c:1600`), which the driver never
  interprets further.
- The actual calibration runs in the firmware.  The host never reads a
  calibration result, a per-chain gain, a temperature compensation table or a
  trimmed frequency offset out of this section.

**Not established:** what the section contains, how large it is on a given
unit, whether the values are per-unit trims or a shared table, and what the
firmware does with them.  The section is opaque at the protocol level: it is
just bytes with a type code, and this project has not decoded them.

## Production data and the write path

The `PRODUCTION` section is read like every other section, and nothing in this
driver path consumes it.  The driver also contains an NVM write path
(`mvm/nvm.c:145-168`), which is used by a manufacturing flow; it has no caller
in this tree, and this project does not write NVM.  The firmware image is
signed, and the NVM is even further out of reach: nothing here should be read
as an invitation to write it.

## What this chapter cannot say

- **The physical layout of the OTP.**  No fuse map, no word addresses, no
  programming voltages or timings.  The host sees sections, not bits.
- **The size and content of each section on this unit.**  The driver reads
  them; this handbook does not record what it read.
- **What calibration values are.**  The section is delivered to the driver and
  handed to the parser, which discards everything except a marker.
- **Whether the version word is enforced anywhere.**  Not on this path; the
  firmware side is not visible here.
- **How to re-provision a unit.**  Out of scope, and not documented in this
  tree.
