# 07 - Errata and workarounds

Two different things live in this chapter and are kept apart:

- **Workarounds the driver carries.**  The driver's own comments call them
  hardware bugs or workarounds; this handbook records the condition, the
  action and where it happens, in the driver's words where the words are
  informative.
- **One firmware assertion this project has reproduced.**  It was measured,
  with a specific host input; what it means inside the firmware is not known
  and is not guessed here.

## The workarounds the driver carries

| # | Condition, in the driver's words | What the driver does | Where |
|---|---|---|---|
| 1 | "L0S states have been found to be unstable with our devices and in newer hardware they are not officially supported at all" | sets the L0S-disabled bit in `CSR_GIO_REG` on every init | `pcie/gen1_2/trans.c:209-214`, bit at `iwl-csr.h:403` |
| 2 | "don't wait for ICH L0s (ICH bug W/A)" | sets `L1A_NO_L0S_RX` in `CSR_GIO_CHICKEN_BITS` | `pcie/gen1_2/trans.c:247-252`, bit at `iwl-csr.h:450` |
| 3 | "Set FH wait threshold to maximum (HW error during stress W/A)" | writes `0xffff0000` to `CSR_DBG_HPET_MEM_REG` | `pcie/gen1_2/trans.c:254-255`, value at `iwl-csr.h:462` |
| 4 | "due to a HW bug in 7260, 3160 and 7265" - the MAC access request must be held while a command is in flight | grabs NIC access before a host command and releases it when the last command completes | field comment `iwl-config.h:153-154`, set at `cfg/7000.c:59`, used at `pcie/gen1_2/tx.c:197` and `pcie/gen1_2/tx.c:1013-1023` |
| 5 | "device may consume much power if FW is not loaded after device reset"; the low-power crystal is off by default after a hardware reset | enables the low-power crystal during teardown, and sets persistence so a shared reset in S3 does not undo it | `pcie/gen1_2/trans.c:322-328`, `pcie/gen1_2/trans.c:352-410`, selected by `cfg/7000.c:136` |
| 6 | 7260/3160 only, unrelated to the flag's name: the oscillator must be forced on to count L1 exit time | the four-read sequence on `OSC_CLK` | `pcie/gen1_2/trans.c:274-293`, selected by `cfg/7000.c:135` |
| 7 | "enable rfkill interrupt: hw bug w/a", in the path where MSI could not be enabled | clears `PCI_COMMAND_INTX_DISABLE` so the interrupt pin is usable | `pcie/gen1_2/trans.c:1659-1667` |
| 8 | "NIC is stuck in a reset state after Early PCIe power off ... causing ME FW to lose ownership" | clears `APMG_PS_CTRL_EARLY_PWR_OFF_RESET_DIS` at init | `mvm/ops.c:135-143`, bit at `iwl-prph.h:35` |
| 9 | Indonesia SKUs: the regulatory data in some 7000-family OTPs wrongly allows wide channels in 5 GHz, and the firmware asserts if they are used | reads the SKU field from the NVM's subsystem-ID copy and force-disables wide channels in 5 GHz for SKU 5 and 9 | `iwl-nvm-parse.c:1419-1448`, called at `iwl-nvm-parse.c:1595` |

Notes on three of them:

- **#2 and #1 are separate.**  One disables the L0S state; the other tells the
  part not to wait for a legacy chipset's L0s exit during the L1-to-L0
  transition.  A port that implements one and not the other has half the
  workaround.
- **#4 is about command delivery, not about power saving.**  The comment in
  the code says the NIC is woken so the firmware will see the host command, and
  the driver holds the access until the last host command completes
  (`pcie/gen1_2/tx.c:1016-1023`).  The consequence for a port: a command
  issued without taking ownership may be dropped by a sleeping part even
  though the doorbell write succeeded.
- **#9 is the only one in the list that can be checked without a card.**  The
  trigger is a field of a value the host already has; the arithmetic is
  `(subsystem_id & 0x1e) >> 1` and the values are 5 and 9.  On the bench card
  the PCI subsystem ID gives 8, so the rule does not fire; the code reads the
  NVM's copy, which this handbook has not recorded.

Two behaviours that look like errata but are ordinary fallbacks are kept out
of the table: the 32-bit DMA mask fallback (`pcie/gen1_2/trans.c:3919-3928`)
and the MSI-to-INTx fallback itself (`pcie/gen1_2/trans.c:1656-1667`).  Only
the rfkill part of the latter is a hardware workaround.

## One firmware assertion, measured

### What was done

On 2026-11-08, arms of the project's own host stack sent `ADD_STA` with
different `station_flags` values while running the same traffic ladder against
the same access point, with the same firmware image (the one in this
repository).  Three values are relevant:

| `station_flags` | What it declares | Result |
|---|---|---|
| `0x00180000` | 20 MHz, one spatial stream | no assertion |
| `0x10180000` | 20 MHz, two spatial streams | no assertion |
| `0x14180000` | 40 MHz, two spatial streams | assertion |

The triggering arms produced this, about 42 s into the run:

```
iwlwifi 0002:21:00.0: Microcode SW error detected.  Restarting 0x2000000.
iwlwifi 0002:21:00.0: Start IWL Error Log Dump:
iwlwifi 0002:21:00.0: Transport status: 0x0000004A, valid: 6
iwlwifi 0002:21:00.0: Loaded firmware version: 17.bfb58538.0 7260-17.ucode
iwlwifi 0002:21:00.0: 0x00002078 | ADVANCED_SYSASSERT
```

Every arm that declared 40 MHz - across several boots - produced this
signature; every arm that declared 20 MHz, including the two-stream one, did
not.  That is the whole of the measured claim: **the 40 MHz declaration in the
station flags is the input that correlates with this assertion on this
firmware**.  The 20 MHz arms bound it from below and the earlier observation
that the assertion also appeared when the port advertised a 40 MHz capability
elsewhere points the same way, but the exact condition inside the firmware is
not established.

### What the code in the message means

Nothing that can be looked up:

- The driver's name table maps a fixed list of assertion codes to strings
  (`fw/img.c:89-112`).
- The lookup masks the code with `FW_SYSASSERT_CPU_MASK` (`fw/img.c:119`,
  definition at `fw/img.h:292`), which is `0xf0000000`.
- `0x2078` masked with that value is still `0x2078`, and no entry in the table
  has that number.  The lookup therefore falls through to the table's last
  entry, the generic name `ADVANCED_SYSASSERT` (`fw/img.c:114-124`).

So the string `ADVANCED_SYSASSERT` in the log is the table's default, not a
description of the failure, and the number `0x2078` carries no host-side
meaning.  This handbook does not attach one to it.  A reader who wants the
meaning needs a firmware-side source - symbols, a decode of the assert path, or
a vendor document - none of which this project has.

## What this chapter cannot say

- **Why 40 MHz upsets the firmware.**  Not established.  The input is known;
  the mechanism is not.
- **Whether the part can run 40 MHz at all.**  No positive 40 MHz run exists
  in this project.  The absence of a working 40 MHz configuration is not
  evidence that the silicon cannot do it, and this handbook does not say
  either way.
- **What the other error-log fields in the dump mean.**  The dump is
  reproduced for its shape and its one code; the remaining fields are firmware
  state words.
- **Whether the firmware would behave differently with another image.**  Only
  one image is in this tree, and it is the only one that was run.
- **Which of the workarounds above are still needed on this part.**  The
  driver applies them unconditionally for the family; this project has not run
  the counterfactual (a card boot without workaround #4, for example).
