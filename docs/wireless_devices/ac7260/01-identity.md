# 01 - Identity

What this part is, in the terms the host can check: the PCI IDs it answers to,
the table entries that give it a name, the firmware it is paired with, and the
source tree those facts are read out of.

## The card as it presents itself

Both commands below were run on the bench card on 2026-11-08; the exact
invocation and the full output are in [`08-observing.md`](08-observing.md).

```
0002:21:00.0 Network controller [0280]: Intel Corporation Wireless 7260 [8086:08b1] (rev 93)
        Interrupts: pin B disabled, MSI(X) routed to IRQ 67
        Region 0: Memory at f0200000 (64-bit, non-prefetchable) [size=8K]
        Capabilities: [d0] MSI: Enable+ Count=1/1 Maskable- 64bit+
                LnkCap: Port #4, Speed 2.5GT/s, Width x1, ASPM L0s L1, ...
                LnkCtl: ASPM L1 Enabled; RCB 64 bytes, ...
                LnkSta: Speed 2.5GT/s, Width x1
        Kernel driver in use: iwlwifi
```

```
iwlwifi 0002:21:00.0: Detected crf-id 0x0, cnv-id 0x0 wfpm id 0x0
iwlwifi 0002:21:00.0: PCI dev 08b1/5070, rev=0x144, rfid=0xd55555d5
iwlwifi 0002:21:00.0: Detected Intel(R) Dual Band Wireless AC 7260
iwlwifi 0002:21:00.0: loaded firmware version 17.bfb58538.0 7260-17.ucode op_mode iwlmvm
iwlwifi 0002:21:00.0: base HW address: 6c:29:95:xx:xx:xx, OTP minor version: 0x0
```

Three things in that output are easy to read the wrong way round:

- `rev 93` and `rev=0x144` are different registers.  The first is the PCI
  configuration-space revision field, 7 bits.  The second is the word the
  driver reads from BAR0 offset `0x028` (`CSR_HW_REV`, `iwl-csr.h:50`) and
  prints at `pcie/gen1_2/trans.c:4293-4295`.  They are not two spellings of one
  number, and neither is derived from the other anywhere in the driver.
- `rfid=0xd55555d5` is the raw `CSR_HW_RF_ID` word, not a decoded RF type.
  What the driver does with it is in [`04-radio.md`](04-radio.md).
- The product name on the third line is chosen by a table entry, not by the
  subsystem ID.  The next two sections show why.

## Two tables with two different jobs

The driver carries two tables that mention this device, and confusing them
produces a false statement about the card.

| Table | Where | Job |
|---|---|---|
| `iwl_hw_card_ids[]` | `pcie/drv.c:225-292` | what the PCI core matches to bind the driver at all.  Every entry's `driver_data` is `iwl7000_mac_cfg`, so this table says nothing about which 7260 variant is present |
| `iwl_dev_info_table[]` | `pcie/drv.c:802-890` | which configuration and which product name the probe selects, after the device is already bound |

`0x5070` appears in the first table (`pcie/drv.c:255`) and **does not appear at
all** in the second.  That is not an oversight: the second table starts with a
wildcard entry for the whole `0x08B1` device, whose comment says what it is
for:

```c
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2ac_name,
		     DEVICE(0x08B1)), // unlisted ones fall through to here
```

A reader who only greps for the subsystem ID in `pcie/drv.c` finds one hit and
concludes the driver knows this variant.  It does not; the hit is the binding
table.

## How the product name is resolved

The chain, in the order the probe runs it:

1. The probe maps BAR0 and reads `CSR_HW_REV`; `0xffffffff` returns `-EIO`
   before anything else happens (`pcie/drv.c:1184-1188`).  This is
   the first "is the card on the bus" gate and it is worth remembering when a
   board misbehaves - see [`08-observing.md`](08-observing.md).
2. It reads `CSR_HW_RF_ID` into `info.hw_rf_id`
   (`pcie/gen1_2/trans.c:4280`).  The blank-OTP fallback that follows
   (`pcie/gen1_2/trans.c:4282-4289`) applies to family 9000 and newer only, so
   this card does not take it.
3. It calls `iwl_pci_find_dev_info()` (`pcie/gen1_2/trans.c:4306-4311`) with
   six keys: device, subsystem device, RF type from `CSR_HW_RF_ID`, the CDB
   bit, and two fields taken from the **subsystem ID**, not from the register:
   `IWL_SUBDEVICE_RF_ID()` and `IWL_SUBDEVICE_BW_LIM()`
   (`iwl-config.h:511-512`).
4. The lookup walks the table **backwards**, from the last entry to the first
   (`pcie/drv.c:1113`).  Backwards is what makes a specific entry win over the
   wildcard and the wildcard win over nothing: the first match found walking
   down is the most specific one.
5. For `0x5070` the specific entries all compare their full subsystem value
   and skip, and the wildcard at `pcie/drv.c:803` matches on device alone.
6. The match assigns the configuration and the name
   (`pcie/gen1_2/trans.c:4313-4314`), and the name is printed as
   `Detected %s` (`pcie/gen1_2/trans.c:4338`).  If nothing matched at all, the
   probe would print `No config found` and fail (`pcie/gen1_2/trans.c:4331`).

Two details make this chain less obvious than it looks.  The RF type passed in
step 3 comes from `CSR_HW_RF_ID`, whose value on this card decodes to a type
that is not one of the named RF types (`04-radio.md` shows the decode); it
costs nothing here because none of the 7260 entries ask for an RF-type match.
And the `rf_id` / `bw_limit` keys come from fields of the subsystem device ID,
not from the RF register, so a subsystem ID reused by another board would select a
different entry even with an identical RF strap.

## The 7260 family as the driver lists it

The two tables below are `iwl_dev_info_table[]` entries `pcie/drv.c:802-890`,
in file order.  Every row says: for this device and subsystem, use this
configuration and print this name.  `any` means the entry does not constrain
the subsystem ID.

Device `0x08B1` (29 entries):

| Subsystem | Configuration | Name |
|---|---|---|
| any | `iwl7260_cfg` | 2ac |
| `0x4060` | `iwl7260_cfg` | 2n |
| `0x406A` | `iwl7260_cfg` | 2n |
| `0x4160` | `iwl7260_cfg` | 2n |
| `0x4062` | `iwl7260_cfg` | n |
| `0x4162` | `iwl7260_cfg` | n |
| `0x4460` | `iwl7260_cfg` | 2n |
| `0x446A` | `iwl7260_cfg` | 2n |
| `0x4462` | `iwl7260_cfg` | n |
| `0x4A70` | `iwl7260_high_temp_cfg` | 2ac |
| `0x4A6E` | `iwl7260_high_temp_cfg` | 2ac |
| `0x4A6C` | `iwl7260_high_temp_cfg` | 2ac |
| `0x4560` | `iwl7260_cfg` | 2n |
| `0x4020` | `iwl7260_cfg` | 2n |
| `0x402A` | `iwl7260_cfg` | 2n |
| `0x4420` | `iwl7260_cfg` | 2n |
| `0xC060` | `iwl7260_cfg` | 2n |
| `0xC06A` | `iwl7260_cfg` | 2n |
| `0xC160` | `iwl7260_cfg` | 2n |
| `0xC062` | `iwl7260_cfg` | n |
| `0xC162` | `iwl7260_cfg` | n |
| `0xC760` | `iwl7260_cfg` | 2n |
| `0xC460` | `iwl7260_cfg` | 2n |
| `0xC462` | `iwl7260_cfg` | n |
| `0xC560` | `iwl7260_cfg` | 2n |
| `0xC360` | `iwl7260_cfg` | 2n |
| `0xC020` | `iwl7260_cfg` | 2n |
| `0xC02A` | `iwl7260_cfg` | 2n |
| `0xC420` | `iwl7260_cfg` | 2n |

Device `0x08B2` (15 entries):

| Subsystem | Configuration | Name |
|---|---|---|
| `0x4270` | `iwl7260_cfg` | 2ac |
| `0x4272` | `iwl7260_cfg` | 2ac |
| `0x4260` | `iwl7260_cfg` | 2n |
| `0x426A` | `iwl7260_cfg` | 2n |
| `0x4262` | `iwl7260_cfg` | n |
| `0x4370` | `iwl7260_cfg` | 2ac |
| `0x4360` | `iwl7260_cfg` | 2n |
| `0x4220` | `iwl7260_cfg` | 2n |
| `0xC270` | `iwl7260_cfg` | 2ac |
| `0xC272` | `iwl7260_cfg` | 2ac |
| `0xC260` | `iwl7260_cfg` | 2n |
| `0xC26A` | `iwl7260_cfg` | n |
| `0xC262` | `iwl7260_cfg` | n |
| `0xC370` | `iwl7260_cfg` | 2ac |
| `0xC220` | `iwl7260_cfg` | 2n |

The bench card's `0x5070` is in neither list; it resolves through the `any`
row, which is the first row of the first table and the last row the reverse
scan visits.

## The three names, and what they do not say

The driver has three strings for this device
(`cfg/7000.c:115-117`):

```c
const char iwl7260_2ac_name[] = "Intel(R) Dual Band Wireless AC 7260";
const char iwl7260_2n_name[] = "Intel(R) Dual Band Wireless N 7260";
const char iwl7260_n_name[] = "Intel(R) Wireless N 7260";
```

The strings are the only statement the tree makes about the difference: the
`2ac` / `2n` / `n` naming convention is a label, not a capability record.
Nothing in the tree says how many antennas a given subsystem has, which bands
its module is wired for, or whether the three names correspond to three dies
or to one die with three module builds.  Band and chain capability are read
from the NVM at attach, and [`04-radio.md`](04-radio.md) shows that path.

## Firmware pairing

| Item | Value | Where |
|---|---|---|
| API range | 17 to 17, both ends | `cfg/7000.c:12` and `cfg/7000.c:18` |
| Name prefix | `iwlwifi-7260` | `cfg/7000.c:37` |
| File name | prefix + `-` + API + `.ucode` | `cfg/7000.c:38` |
| Configuration field | `.fw_name_pre = IWL7260_FW_PRE` | `cfg/7000.c:128` |
| NVM version constant | `0x0a1d` | `cfg/7000.c:24` |
| DCCM window | offset `0x800000`, length `0x14000` | `cfg/7000.c:32-33` |
| Shipped image | `iwlwifi-7260-17.ucode`, 1,049,340 bytes, sha256 `5d81a6003df0228a497ad27f916ba2c979614b4c439b0f45a5f2873dc0607fe8` | `firmware/iwlwifi/` |
| Version string | `17.bfb58538.0` | measured, see above |

The API range being a single value is why the pairing is exact: there is one
firmware file this driver will ask for, and the container's own version word
must be 17 (decoded in [`06-firmware.md`](06-firmware.md)).  The NVM version
constant is a different thing entirely and is *not* compared on this path; see
[`05-nvm-otp.md`](05-nvm-otp.md) for what actually happens to it.

## The configuration object

`iwl7260_cfg` (`cfg/7000.c:127-138`) is the per-variant configuration.  It
inherits from two macros and adds the radio declaration:

| Field | Value | Meaning in this tree |
|---|---|---|
| `.led_mode` | `IWL_LED_RF_STATE` | the LED follows the radio state |
| `.non_shared_ant` | `ANT_A` | antenna A is the WiFi-only one |
| `.dccm_offset` | `0x800000` | where the data SRAM window sits |
| `.ucode_api_max` / `.min` | 17 / 17 | see above |
| `.ht_params.stbc` | `true` | 802.11n space-time block coding is declared |
| `.ht_params.ht40_bands` | 2 GHz and 5 GHz | 40 MHz operation is declared for both bands |
| `.nvm_ver` | `0x0a1d` | compared by the DVM op mode, not by `iwlmvm` |
| `.host_interrupt_operation_mode` | `true` | see [`02-silicon.md`](02-silicon.md) |
| `.lp_xtal_workaround` | `true` | see [`02-silicon.md`](02-silicon.md) |
| `.dccm_len` | `0x14000` (80 KiB) | data SRAM window length |

The macro that supplies the first three, `IWL_DEVICE_7000` and
`IWL_DEVICE_7000_COMMON`, is at `cfg/7000.c:90-98`.  The family parameters
that are shared by every 7000-series part (`iwl7000_base`,
`cfg/7000.c:52-60`) are covered in [`02-silicon.md`](02-silicon.md) and
[`03-bus.md`](03-bus.md); they are where the queue counts and the DMA window
come from.

## Where the source comes from

| Item | Value |
|---|---|
| Upstream | mainline Linux `drivers/net/wireless/intel/iwlwifi/` |
| Pin | `e71d9a28760f605082308c2e1114627d406b4a29` (7.3.0-rc2) |
| Patch level | zero; the tree is byte-identical to the pin |
| Vendored files | 294, each with a digest in `wireless-drivers/iwlwifi/patches/MANIFEST.sha256` |
| Verify | `cd wireless-drivers/iwlwifi && sha256sum -c patches/MANIFEST.sha256` |
| First-party files | `ports/wlan_iwl_linux.c`, `ports/mac80211_iwl_closure.c`, `sources.mk` |
| Provenance record | `wireless-drivers/iwlwifi.provenance.md` |

The 294 is the count the digest manifest itself carries, and the verify
command above is how it was checked; the three first-party files are not part
of the vendored set, which is why the directory has 299 files in total.  A
reader who wants the count to be right should run the verify command rather
than trust a number in prose, including this one.

## What this chapter cannot say

- **What physically separates the three names.**  The tree stores strings and
  subsystem IDs; it does not store module builds, antenna counts or band
  wiring per subsystem.  Anything stronger would be invented.
- **Whether the bench module has two receive chains.**  That is a runtime NVM
  property, and this handbook does not have a measurement for this card.
- **Whether `0x08B2` is a different die.**  Neither the driver nor any public
  document available here says.  It is a separate device ID with a separate
  entry block and nothing more.
- **Anything about the retail packaging of a given subsystem ID.**  The
  mapping from subsystem ID to product is a vendor catalogue fact, not a
  driver fact.
