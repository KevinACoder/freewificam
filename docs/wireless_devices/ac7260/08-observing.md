# 08 - Observing this part

Every `Measured here` claim in this handbook comes from something in this
chapter.  The commands are the ordinary host commands for a PCIe wireless
device; none of them needs a private tool, and none of them was run through a
debugger.

Three rules apply to everything below:

- **One command, one piece of evidence.**  If a claim needs two observations,
  both are written down.
- **The observer must not change the device.**  Reading a status register is
  not a state change; writing one is.  Everything in this chapter is read-only
  unless it is labelled as a driver action.
- **Absence is evidence too.**  A register that cannot be read, or a device
  that is not on the bus, is a result and is recorded as one.

## Is the card there at all?

The first question is always whether the endpoint is on the bus, and it is
answered twice, at two levels.

### The PCI level

```
$ lspci -vv -d 8086:08b1
0002:21:00.0 Network controller [0280]: Intel Corporation Wireless 7260 [8086:08b1] (rev 93)
        Interrupts: pin B disabled, MSI(X) routed to IRQ 67
        Region 0: Memory at f0200000 (64-bit, non-prefetchable) [size=8K]
        Capabilities: [d0] MSI: Enable+ Count=1/1 Maskable- 64bit+
                LnkCap: Port #4, Speed 2.5GT/s, Width x1, ASPM L0s L1, Exit Latency L0s <4us, L1 <32us
                LnkCtl: ASPM L1 Enabled; RCB 64 bytes, LnkDisable- CommClk+
                LnkSta: Speed 2.5GT/s, Width x1
                LnkSta2: Current De-emphasis Level: -3.5dB, EqualizationComplete- EqualizationPhase1-
        Kernel driver in use: iwlwifi
```

The line was taken from the bench board on 2026-11-08; the surrounding lines
that `lspci` prints for control and status registers are omitted here because
they are properties of the bridge above the card, not of the card.  The device
line, region, interrupt and link lines are quoted as printed.

What this answers: identity (`8086:08b1`, class `0280`), the config-space
revision (`93`), the memory window (8 KiB), the interrupt (one MSI, IRQ 67),
and the trained link (2.5GT/s, one lane).

### The driver level

```
$ dmesg | grep iwlwifi
iwlwifi 0002:21:00.0: Detected crf-id 0x0, cnv-id 0x0 wfpm id 0x0
iwlwifi 0002:21:00.0: PCI dev 08b1/5070, rev=0x144, rfid=0xd55555d5
iwlwifi 0002:21:00.0: Detected Intel(R) Dual Band Wireless AC 7260
iwlwifi 0002:21:00.0: loaded firmware version 17.bfb58538.0 7260-17.ucode op_mode iwlmvm
iwlwifi 0002:21:00.0: base HW address: 6c:29:95:xx:xx:xx, OTP minor version: 0x0
```

The MAC address's last three octets are elided in this handbook; they are the
bench card's, not a property of the part.  Everything else is as printed.

What this answers: the RF/CNV identifiers (all zero), the word read from
`CSR_HW_RF_ID` (`0xd55555d5`), the word read from `CSR_HW_REV` (`0x144`, which
is **not** the `0x93` from `lspci`), the selected product name, the firmware
image and its version string, and the OTP minor version register.

### The register-level presence check

The driver performs one more check before it trusts the device: it maps BAR0
and reads `CSR_HW_REV` at offset `0x028`; a value of `0xffffffff` returns
`-EIO` and the probe stops (`pcie/drv.c:1184-1188`).  That is the check to
imitate: if the register reads all ones, the problem is between the host and
the card, and no driver-side change will fix it.

Two facts follow from having both levels:

- `rev 93` (config space, `lspci`) and `rev=0x144` (`CSR_HW_REV`, driver) are
  two different registers.  A statement that "the revision is 0x93" and a
  statement that "the revision is 0x144" can both be true.
- The product name is a table lookup, not a register read; see
  [`01-identity.md`](01-identity.md).

## The register surfaces

| Surface | How to reach it | What it is good for |
|---|---|---|
| configuration space | `lspci`, `setpci` | link, interrupt, BAR, power-management state |
| BAR0 windows | the driver's own mappings; there is no generic user command | CSR block, FIFO window, indirect PRPH |
| debugfs | `/sys/kernel/debug/iwlwifi/...` when the driver has debugfs enabled | driver-maintained state, SRAM window, temperature |
| tracefs | `/sys/kernel/debug/tracing/events/iwlwifi/` | host command and RX packet events |
| dmesg | the kernel log | identity, firmware load, error dumps |

The window layout is in [`03-bus.md`](03-bus.md); the PRPH and SHR access
protocols are in [`02-silicon.md`](02-silicon.md).  This handbook does not ship
a BAR0 poke tool: on Linux the mapping belongs to the driver, and a second
mapping of the same window is a good way to change the device's state without
noticing.

## Interrupts and packets

Trace events are the cheapest way to see host commands and received packets
without changing the driver.  The two that matter here:

| Event | Declared at | Shows |
|---|---|---|
| `iwlwifi_dev_hcmd` | `iwl-devtrace-iwlwifi.h:18` | host commands: id, group, length, and the header |
| `iwlwifi_dev_rx` | `iwl-devtrace-iwlwifi.h:52` | received packets, including firmware notifications |

They exist only if the kernel was built with the tracing option
(`Kconfig:139`, `IWLWIFI_DEVICE_TRACING`) **and** the kernel's own tracing
infrastructure is enabled - the driver's tracing option depends on it, and a
kernel without `FTRACE` silently has no events at all.

```
$ ls /sys/kernel/debug/tracing/events/iwlwifi/
$ echo 1 > /sys/kernel/debug/tracing/events/iwlwifi/iwlwifi_dev_hcmd/enable
$ cat /sys/kernel/debug/tracing/trace_pipe
```

(On a kernel that mounts tracefs at `/sys/kernel/tracing`, the same files are
there.)  Turn the event off when finished: the command event fires for every
host command, and a busy command stream will fill the buffer.

The debugfs directory exposes driver state rather than device state.  The
useful entries for this part are the ones the mvm debug code creates:
`sram`, `nic_temp`, `set_nic_temperature` and `tx_flush`
(`mvm/debugfs.c:2116-2119`).  They are a window into what the driver already
believes, not an independent instrument, and the `sram` file reads through the
driver's own access path.

## Reading a firmware assertion

When the firmware asserts, the driver prints an error log dump and restarts the
firmware.  The shape, from the run that produced it (2026-11-08):

```
iwlwifi 0002:21:00.0: Microcode SW error detected.  Restarting 0x2000000.
iwlwifi 0002:21:00.0: Start IWL Error Log Dump:
iwlwifi 0002:21:00.0: Transport status: 0x0000004A, valid: 6
iwlwifi 0002:21:00.0: Loaded firmware version: 17.bfb58538.0 7260-17.ucode
iwlwifi 0002:21:00.0: 0x00002078 | ADVANCED_SYSASSERT
```

Read it in this order:

1. **`Restarting`** - the firmware is being restarted.  A device that keeps
   working after this line is working *after a restart*; do not treat the run
   as continuous.
2. **`Transport status`** - a transport-layer word, not an assertion code.
3. **`Loaded firmware version`** - confirms which image asserted.
4. **The first `| <name>` line** - the assertion code and the driver's name
   for it.  If the name is `ADVANCED_SYSASSERT`, the driver had no name for
   that code; see [`07-errata.md`](07-errata.md).
5. The remaining lines of the dump are the firmware's register state.  Their
   field names are the driver's names; this handbook does not interpret them.

## The firmware image

```
$ sha256sum firmware/iwlwifi/iwlwifi-7260-17.ucode
5d81a6003df0228a497ad27f916ba2c979614b4c439b0f45a5f2873dc0607fe8  firmware/iwlwifi/iwlwifi-7260-17.ucode
$ python3 tools/fw/iwl-tlv.py firmware/iwlwifi/iwlwifi-7260-17.ucode
```

The decode is in [`06-firmware.md`](06-firmware.md) and the tool's self-test
runs without a card.  One caution that cost time here: the container header is
**88 bytes**, not 80.  An 80-byte header parse reads the reserved field as a
zero-length record and reports one extra record while still walking the rest
correctly, which is the kind of error that hides until someone compares record
counts.

## What surface answers which question

| Question | Surface | Chapter |
|---|---|---|
| Is the card on the bus? | `lspci`, BAR0 `CSR_HW_REV` | 01, 03 |
| Which variant and name? | `dmesg` identity lines | 01 |
| Which firmware? | `dmesg`, image digest, TLV decode | 06 |
| What is the link doing? | `lspci -vv` link registers | 03 |
| Did a host command go out? | trace event `iwlwifi_dev_hcmd` | 06 |
| Did the firmware report anything? | trace event `iwlwifi_dev_rx` | 06 |
| Why did it stop? | dmesg assert dump | 07 |
| What is the driver's state? | debugfs entries | 08 |

## What this chapter cannot say

- **What the die is doing.**  There is no JTAG, no firmware symbol table and
  no debugger attachment.  Everything here is observed through the host.
- **That any register is safe to poke.**  The chapter lists registers the
  driver reads or writes; it does not certify an arbitrary offset.  The only
  safe general rule is: read status, do not write.
- **Anything about RF behaviour.**  No conducted measurement, no spectrum
  analyser, no power meter.  An over-the-air capture shows which rate and
  which aggregation were used; it does not measure the transmitter.
- **Which workarounds are still necessary.**  The driver applies them for the
  family; the counterfactual was not run.
