# 06 - Firmware

The image the host loads: what the file is, how the driver decides which one to
ask for, what the image says about itself, and which of its declarations change
driver behaviour.  This chapter stops at the container.  The command and
notification layouts themselves are the driver headers under
`wireless-drivers/iwlwifi/fw/api/`, which are the authority.

## Which file, and how it is chosen

The driver builds the file name from two configuration fields and one version
number:

1. The prefix comes from the configuration, `iwlwifi-7260`
   (`cfg/7000.c:37`, selected at `cfg/7000.c:128`).
2. The driver starts at the maximum supported API and counts down to the
   minimum (`iwl-drv.c:350-357`), which for this part is 17 to 17
   (`cfg/7000.c:12`, `cfg/7000.c:18`).
3. The name is assembled as prefix, dash, API, `.ucode`
   (`iwl-drv.c:377-379`), and loaded with `request_firmware_nowait()`
   (`iwl-drv.c:384-386`).

The result is one possible name and one required version.  The image in this
tree is `firmware/iwlwifi/iwlwifi-7260-17.ucode`, 1,049,340 bytes, sha256
`5d81a6003df0228a497ad27f916ba2c979614b4c439b0f45a5f2873dc0607fe8`, and the
driver printed exactly that name at attach
(`loaded firmware version 17.bfb58538.0 7260-17.ucode`, measured; see
[`08-observing.md`](08-observing.md)).

## The container

The file is a header followed by records.  Both structures are declared in
`fw/file.h`:

| Part | Layout | Size |
|---|---|---|
| header | `zero` (a zero word), `magic` = `0x0a4c5749`, 64-byte human-readable string, `ver`, `build`, 8-byte reserved field | 88 bytes |
| record | `type`, `length`, payload, padded up to a multiple of 4 | 8 bytes plus payload |

Header at `fw/file.h:138-159`, record at `fw/file.h:129-133`, magic and string
size at `fw/file.h:135-136`, padding rule in the header comment at
`fw/file.h:152-157`.  The parser is `iwl_parse_tlv_firmware()`
(`iwl-drv.c:792`): it checks the magic and the length, then walks records at
`iwl-drv.c:846-868`, rejecting a record whose declared length or padded length
would run past the end of the file.

**A parsing trap worth writing down.**  The header is 88 bytes, not 80: the
`ignore` field at the end of the structure is real, and the first record starts
at offset 88.  A decoder that assumes an 80-byte header reads those eight zero
bytes as a zero-length record of type 0 and then starts the next record eight
bytes late; in this image the next record begins at that later offset, so the
rest of the walk still lands on record boundaries and the only visible
symptom is one extra record.  This handbook's appendix was
produced with the 88-byte layout; the first record is at offset 88.

## What the image says about itself

Two different version strings are in play, and they are not the same one:

- The header's `ver` word is `0x00000011`, which the driver decodes into major,
  minor, API and serial fields (`iwl-drv.c:833-840`, decoders at
  `fw/file.h:590-591`).  On the bench card: 17 in the low byte fields, matching
  the required API.
- The `human_readable` string in the header is `N/A::bfb58538`.  It is copied
  into the driver's state (`iwl-drv.c:824-825`) but it is not the string the
  driver logs.
- The logged string comes from the `FW_VERSION` record (type 36), which carries
  three words - major, minor, local component - and is printed as
  `major.minor8.local` (`iwl-drv.c:1083-1099`).  The image's record decodes to
  major 17, minor `0xbfb58538`, local 0, which is the `17.bfb58538.0` in the
  log.

So: "the firmware version is 17" is a statement about the API gate; the
`17.bfb58538.0` string is a statement about this build.

## The capability bit vectors

Two records carry bit vectors, and single bits of them gate whole code paths:

| Record | Type | Meaning |
|---|---|---|
| `API_CHANGES_SET` | 29 | the command-API changes the firmware implements |
| `ENABLED_CAPABILITIES` | 30 | the firmware's capabilities |

The parsers store one word per record at a word index, so a second record of
the same type with a different index extends the bitmap
(`iwl-drv.c:544-560`, and the storage in `fw/img.h:58-59`).  The gates are two
inline helpers that test one bit: `fw_has_api()` and `fw_has_capa()`
(`fw/img.h:68-80`).  The bit numbers are enumerators in `fw/file.h`: the API
list starts at `fw/file.h:274`, the capability list at `fw/file.h:458`.

This image sets:

- API bits: 0, 1, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 17, 19, 20, 23, 24,
  26, 27
- capability bits: 4, 12, 16, 18, 24

Two bits that are **absent** matter for the receive aggregation path, and the
consequences are entirely inside the driver:

- API bit 30 (`IWL_UCODE_TLV_API_STA_TYPE`, `fw/file.h:282`) is clear, and
  capability bit 68 (`IWL_UCODE_TLV_CAPA_MULTI_QUEUE_RX_SUPPORT`,
  `fw/file.h:512`) is clear.
- Therefore `iwl_mvm_has_new_rx_api()` returns false (`mvm/mvm.h:1485-1489`),
  and `iwl_mvm_add_sta_cmd_size()` selects the older, 44-byte `ADD_STA` body
  (`mvm/sta.c:18-25`).
- The block-ack bookkeeping that only exists on the new receive API is never
  reached; the code that would check the firmware's block-ack ID is not
  evaluated (`mvm/sta.c:2792-2794`).

None of that is a statement about the firmware's intent.  It is what this
container declares, and what this driver does with the declaration.

## Command versions

`CMD_VERSIONS` (type 48) overrides the version number the driver uses for a
host command.  Each entry is four bytes - command, group, command version,
notification version (`fw/file.h:1033-1040`) - and the driver keeps the array
as given (`iwl-drv.c:1401-1420`).  A lookup that finds no entry returns the
caller's default (`fw/img.c:42-60`), which is how a firmware that predates the
mechanism still works.

This image carries 26 entries, all in group 0, with notification version 0;
the appendix lists them.  Their practical meaning: a port that builds a command
body for a fixed version may be sending a layout this firmware does not expect,
and this table is where the expectation is written down.

## The other declarations that change behaviour

| Record | Value in this image | What the driver does with it |
|---|---|---|
| `NUM_OF_CPU` (27) | 1 | one CPU image; the driver sets the dual-CPU flag only for 2 and rejects values above 2 (`iwl-drv.c:1059-1080`) |
| `N_SCAN_CHANNELS` (31) | 40 | stored as the scan channel count (`iwl-drv.c:1081-1086`) |
| `FLAGS` (18) | `0x59fb4b7f` | a capability bitmap: the parser collects the words (`iwl-drv.c:921-940`), and the op mode tests individual bits such as U-APSD support (`mvm/mac80211.c:3421`) and short-beacon support (`mvm/scan.c:545`) |
| `DEF_CALIB` (22), four of them | 12 bytes each | `ucode_type` plus two trigger bitmaps (`iwl-drv.c:402-411`), stored per image type (`iwl-drv.c:526-542`) and sent later as part of the PHY configuration command (`fw/file.h:594-595`) |
| `SEC_RT`, `SEC_INIT`, `SEC_WOWLAN`, `SEC_RT_USNIFFER` | 12 sections, three per image | the code and data sections; stored by the driver, not interpreted |
| `UNKNOWN(35)` | 4 bytes | the driver has no name for type 35 and logs it only at debug level (`iwl-drv.c:1476`), then ignores it |

`DEF_CALIB` is the closest the host gets to calibration: it carries two
bitmaps that tell the firmware which calibration flows to run.  The results
never come back to the host; see [`05-nvm-otp.md`](05-nvm-otp.md).

## Appendix A - the image in this tree

Produced by `tools/fw/iwl-tlv.py` on
`firmware/iwlwifi/iwlwifi-7260-17.ucode`; the summary line the tool prints is
reproduced above.  Large payloads are truncated to their first 16 bytes.

| Offset | Type | Name | Length | Payload |
|---|---|---|---|---|
| 88 | 6 | PROBE_MAX_LEN | 4 | 0x00000200 |
| 100 | 7 | MEM_DESC | 0 | - |
| 108 | 18 | FLAGS | 4 | 0x59fb4b7f |
| 120 | 29 | API_CHANGES_SET | 8 | word 0 0x0d9afcfb bits 0, 1, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 17, 19, 20, 23, 24, 26, 27 |
| 136 | 30 | ENABLED_CAPABILITIES | 8 | word 0 0x01051010 bits 4, 12, 16, 18, 24 |
| 152 | 30 | ENABLED_CAPABILITIES | 8 | word 2 0x00000000 bits (none) |
| 168 | 28 | CSCHEME | 16 | 00000000000000000000000000000000 |
| 192 | 27 | NUM_OF_CPU | 4 | 0x00000001 |
| 204 | 19 | SEC_RT | 81924 | 0000800000000000110000003885b5bf... |
| 82136 | 19 | SEC_RT | 180228 | 000000002020800f000040002020800f... |
| 262372 | 19 | SEC_RT | 36 | ccccffff2020800f000040002020800f... |
| 262416 | 20 | SEC_INIT | 81924 | 0000800000000000110000003885b5bf... |
| 344348 | 20 | SEC_INIT | 180228 | 000000002020800f000040002020800f... |
| 524584 | 20 | SEC_INIT | 36 | ccccffff2020800f000040002020800f... |
| 524628 | 21 | SEC_WOWLAN | 81924 | 0000800000000000110000003885b5bf... |
| 606560 | 21 | SEC_WOWLAN | 180228 | 000000002020800f000040002020800f... |
| 786796 | 21 | SEC_WOWLAN | 36 | ccccffff2020800f000040002020800f... |
| 786840 | 34 | SEC_RT_USNIFFER | 81924 | 0000800000000000110000003885b5bf... |
| 868772 | 34 | SEC_RT_USNIFFER | 180228 | 000000002020800f000040002020800f... |
| 1049008 | 34 | SEC_RT_USNIFFER | 36 | ccccffff2020800f000040002020800f... |
| 1049052 | 22 | DEF_CALIB | 12 | 00000000fb158c0033110000 |
| 1049072 | 22 | DEF_CALIB | 12 | 01000000fb158c0000000000 |
| 1049092 | 22 | DEF_CALIB | 12 | 02000000fb158c0033110000 |
| 1049112 | 22 | DEF_CALIB | 12 | 03000000fb158c0033110000 |
| 1049132 | 23 | PHY_SKU | 4 | 0x00330018 |
| 1049144 | 31 | N_SCAN_CHANNELS | 4 | 0x00000028 |
| 1049156 | 48 | CMD_VERSIONS | 104 | 26 x 4 B entries |
| 1049268 | 35 | UNKNOWN(35) | 4 | 00704000 |
| 1049280 | 50 | FW_GSCAN_CAPA | 32 | 00100000100000001000000008000000... |
| 1049320 | 36 | FW_VERSION | 12 | major 17, minor 0xbfb58538, local 0 |

The command versions this image carries (type 48):

| # | cmd | group | cmd_ver | notif_ver |
|---|---|---|---|---|
| 1 | 152 | 0 | 1 | 0 |
| 2 | 155 | 0 | 6 | 0 |
| 3 | 78 | 0 | 1 | 0 |
| 4 | 119 | 0 | 6 | 0 |
| 5 | 212 | 0 | 3 | 0 |
| 6 | 1 | 0 | 3 | 0 |
| 7 | 136 | 0 | 2 | 0 |
| 8 | 93 | 0 | 2 | 0 |
| 9 | 72 | 0 | 1 | 0 |
| 10 | 30 | 0 | 1 | 0 |
| 11 | 44 | 0 | 1 | 0 |
| 12 | 82 | 0 | 1 | 0 |
| 13 | 126 | 0 | 1 | 0 |
| 14 | 159 | 0 | 3 | 0 |
| 15 | 25 | 0 | 2 | 0 |
| 16 | 40 | 0 | 1 | 0 |
| 17 | 156 | 0 | 1 | 0 |
| 18 | 213 | 0 | 4 | 0 |
| 19 | 9 | 0 | 1 | 0 |
| 20 | 41 | 0 | 1 | 0 |
| 21 | 81 | 0 | 1 | 0 |
| 22 | 8 | 0 | 1 | 0 |
| 23 | 209 | 0 | 3 | 0 |
| 24 | 24 | 0 | 7 | 0 |
| 25 | 169 | 0 | 1 | 0 |
| 26 | 43 | 0 | 1 | 0 |

## Appendix B - reproducing the decode

```
python3 tools/fw/iwl-tlv.py firmware/iwlwifi/iwlwifi-7260-17.ucode
python3 tools/fw/iwl-tlv.py --json firmware/iwlwifi/iwlwifi-7260-17.ucode
python3 tools/fw/iwl-tlv.py --selftest
```

If the tool is not at hand, the format is enough to do it by hand: read the
88-byte header, then from offset 88 read an 8-byte record header, take
`length` bytes of payload, skip `length` rounded up to a multiple of four, and
repeat while the offset is at least 8 bytes short of the file end.  The type
names are the enumerators of `enum iwl_ucode_tlv_type` (`fw/file.h:47-127`);
the bit numbers are the enumerators of the two capability enums
(`fw/file.h:274`, `fw/file.h:458`).

## What this chapter cannot say

- **What the firmware does.**  The container is described; the code inside the
  sections is not decoded, and no firmware symbol is available.
- **Why the section payloads are the size they are.**  The three-part split of
  each of the four images is a build artefact of the vendor's tool chain.
- **What the calibration triggers select.**  They are bitmaps; their bit
  meanings are firmware-internal.
- **What the FLAGS word's other bits mean.**  Only the PAN bit is named by the
  driver.
- **What type 35 is.**  The driver does not know either, and this handbook
  does not guess.
- **Why the assertion at 40 MHz happens.**  That is a firmware behaviour; see
  [`07-errata.md`](07-errata.md) for what was measured and what is not known.
