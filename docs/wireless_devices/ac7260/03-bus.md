# 03 - Bus

How the host talks to the part: the PCIe link, the memory windows behind
BAR0, the interrupt it gets, the DMA constraints, and the two descriptor
rings.  Everything here is either measured on the bench card or read out of
the driver; nothing is taken from a PCIe specification as if it were a fact
about this part.

## The link as it presents itself

From `lspci -vv` on the bench card (the full block is in
[`08-observing.md`](08-observing.md)):

```
0002:21:00.0 Network controller [0280]: Intel Corporation Wireless 7260 [8086:08b1] (rev 93)
        Interrupts: pin B disabled, MSI(X) routed to IRQ 67
        Region 0: Memory at f0200000 (64-bit, non-prefetchable) [size=8K]
        Capabilities: [d0] MSI: Enable+ Count=1/1 Maskable- 64bit+
                LnkCap: Port #4, Speed 2.5GT/s, Width x1, ASPM L0s L1, Exit Latency L0s <4us, L1 <32us
                LnkCtl: ASPM L1 Enabled; RCB 64 bytes, LnkDisable- CommClk+
                LnkSta: Speed 2.5GT/s, Width x1
        Kernel driver in use: iwlwifi
```

Four facts a port has to plan around:

- **One lane at 2.5GT/s.**  The capability register and the status register
  agree, so this is not a down-trained link.  The bench board's slot is
  electrically x2 (`docs/boards/rk3568-itx.md:91`); the endpoint is what
  limits the link.
- **BAR0 is 8 KiB, 64-bit, non-prefetchable.**  That is exactly the size of
  the window layout below: the FIFO window ends at `0x2000` (`iwl-fh.h:22-23`),
  which is the end of an 8 KiB mapping.  The arithmetic is a check on the
  layout, not an independent measurement.
- **One MSI vector**, and interrupt pin B is disabled.
- **ASPM L1 is enabled on the link**, the driver forces L0S off, and it
  deliberately leaves L1 alone - see the ASPM section.

## The windows behind BAR0

| Range | Contents | Access | Defined at |
|---|---|---|---|
| `0x000` - `0x3ff` | CSR block: reset, clock, interrupt, host-bus control, power | direct 32-bit loads and stores | `iwl-csr.h:29-46` |
| `0x400` - `0x4ff` | host-bus block: the address and data registers used to reach memory and PRPH | direct 32-bit loads and stores | `iwl-csr.h:512-558` |
| `0x1000` - `0x1fff` | FIFO window: TX/RX DMA configuration, ring base addresses, doorbell | direct 32-bit loads and stores | `iwl-fh.h:22-23` |

The host-bus registers that matter:

| Offset | Register | Purpose |
|---|---|---|
| `0x40c` | `HBUS_TARG_MEM_RADDR` | set an address in the device's memory space for a read |
| `0x410` | `HBUS_TARG_MEM_WADDR` | set an address for a write |
| `0x418` | `HBUS_TARG_MEM_WDAT` | write data |
| `0x41c` | `HBUS_TARG_MEM_RDAT` | read data |
| `0x444` | `HBUS_TARG_PRPH_WADDR` | set a PRPH address for a write |
| `0x448` | `HBUS_TARG_PRPH_RADDR` | set a PRPH address for a read |
| `0x44c` | `HBUS_TARG_PRPH_WDAT` | write data |
| `0x450` | `HBUS_TARG_PRPH_RDAT` | read data |
| `0x460` | `HBUS_TARG_WRPTR` | doorbell: hand the device a new write pointer |

Offsets from `iwl-csr.h:523-558`; the PRPH access protocol itself is in
[`02-silicon.md`](02-silicon.md), because it is a bus to a register space
rather than a register.

## Interrupts

This part gets one MSI vector, and the driver's code says why: the MSI-X path
is only entered when the configuration reports multi-queue receive support,
and `0x7000` does not (`pcie/gen1_2/trans.c:1603-1604`, jumping to the MSI
label at `pcie/gen1_2/trans.c:1656`).  The MSI setup then sets
`info->num_rxqs = 1` and calls `pci_enable_msi()`
(`pcie/gen1_2/trans.c:1657-1658`).  For a port, the consequence is that there
is one interrupt for everything: command responses, RX packets, TX
completions, firmware notifications and error conditions all arrive on it.

If MSI cannot be enabled, the driver falls back to INTx - and the fallback
carries a workaround: it clears `PCI_COMMAND_INTX_DISABLE` with the comment
"enable rfkill interrupt: hw bug w/a"
(`pcie/gen1_2/trans.c:1659-1667`).  That comment is one of the entries in
[`07-errata.md`](07-errata.md).

### What one interrupt does

`iwl_pcie_irq_handler()` (`pcie/gen1_2/rx.c:1895`) opens by taking the
interrupt lock and then asks for the cause in one of two ways
(`pcie/gen1_2/rx.c:1909-1913`):

- through the interrupt coalescing table, a host-memory array that the device
  writes cause words into (`pcie/gen1_2/rx.c:1762-1779`; allocated at
  `pcie/gen1_2/rx.c:2185`), or
- through the legacy path that reads `CSR_INT` and `CSR_FH_INT_STATUS`
  (`pcie/gen1_2/rx.c:1920-1921`).

The cause is then masked with the driver's software mask
(`pcie/gen1_2/rx.c:1928`), so "the device raised it" and "we handle it" are
two different bits of state.  FIFO causes are acknowledged by writing
`CSR_FH_INT_STATUS` (`pcie/gen1_2/rx.c:2068`,
`pcie/gen1_2/rx.c:2114`), and the top half turns the mask off on entry (`pcie/gen1_2/rx.c:2246-2258`)
while the threaded handler re-enables it (`pcie/gen1_2/internal.h:995-1003`).

## DMA

| Property | Value | Source |
|---|---|---|
| TFD address size | 36 bits for this generation (64 bits on the newer one) | `pcie/gen1_2/trans.c:3825-3833` |
| Driver's DMA mask request | `DMA_BIT_MASK(36)`, then `DMA_BIT_MASK(32)` if that fails | `pcie/gen1_2/trans.c:3919-3928` |
| Fallback if both fail | the probe fails with "No suitable DMA available" | `pcie/gen1_2/trans.c:3925-3926` |
| RX buffer alignment | low 12 bits of the buffer's DMA address must be zero | `pcie/gen1_2/trans.c:3835`, checked at `pcie/gen1_2/rx.c:270` |
| Descriptor memory | coherent allocations; the device reads and writes them directly | `pcie/gen1_2/tx.c`, `pcie/gen1_2/rx.c` |
| Transmit DMA mask constant | `DMA_BIT_MASK(36)` | `iwl-fh.h:597` |

The alignment row is worth expanding: the driver does not merely request a
4 KiB-aligned buffer, it *asserts* the alignment every time a buffer is
restocked, with a `WARN_ON` (`pcie/gen1_2/rx.c:269-270`).  A port that gets
this wrong will produce a warning under load rather than an obvious failure,
and the buffer will still be handed to the device, so the symptom is corrupt
receive data rather than a probe error.

## The two rings

### Transmit: TFD rings

| Item | Value | Source |
|---|---|---|
| Queues | 31 | `cfg/7000.c:54` |
| Entries per queue | 256 | `cfg/7000.c:55` |
| Entries usable | 255 - the comment is explicit that the 256th cannot be used | `iwl-fh.h:679` |
| Entry size | 128 bytes for this generation; 256 for the newer one | `iwl-fh.h:662-667` |
| Ring size per queue | 256 x 128 = 32 KiB | arithmetic from the two rows above |
| Buffers per entry | up to 20, each up to 4092 bytes, concatenated into a frame of up to 8 KiB | `iwl-fh.h:672-677`, `iwl-fh.h:598` |
| Entry layout | 3 reserved bytes, a buffer count, 20 buffer descriptors of 4 + 2 bytes, 4 bytes of padding | `iwl-fh.h:692-697` |
| Ring base registers | `FH_MEM_CBBC_0_15_LOWER_BOUND` plus 4 bytes per queue | `iwl-fh.h:61-79` |
| Queue enable | `FH_TCSR_CHNL_TX_CONFIG_REG(n)`, enable value `0x80000000` | `iwl-fh.h:472-473`, `iwl-fh.h:495` |
| Doorbell | write the producer index to `HBUS_TARG_WRPTR` | below |

The queue-count and ring-depth numbers come from the family parameters, not
from a register read: `iwl7000_base` declares 31 queues of at most 256
descriptors (`cfg/7000.c:52-60`).  Whether a particular piece of silicon
populates all 31 is not something the driver checks.

### Receive: one ring of buffer descriptors

| Item | Value | Source |
|---|---|---|
| Rings | one, queue 0 | `iwl-fh.h:568` |
| Entries | 256 | `iwl-fh.h:564` |
| Descriptor status block | four 16-bit counters plus a spare word | `iwl-fh.h:581-587` |
| Ring base and write pointer | `FH_RSCSR_CHNL0_RBDCB_BASE_REG`, `FH_RSCSR_CHNL0_RBDCB_WPTR_REG` | `iwl-fh.h:231-242` |
| Configuration register | `FH_MEM_RCSR_CHNL0_CONFIG_REG`; enable value `0x80000000`, buffer size in bits 17:16 | `iwl-fh.h:275-297` |
| Stop handshake | clear the config register, then poll `FH_MEM_RSSR_RX_STATUS_REG` bit 24 until idle | `iwl-fh.h:303-324` |
| Write-pointer update rule | only when the free count crosses a multiple of 8 | `pcie/gen1_2/rx.c:278-286` |

The multiple-of-8 rule is the kind of detail that only shows up as a
mysterious stall: the device is told about new receive buffers in groups, so a
port that writes the pointer on every buffer is making more doorbell traffic
than the driver does, and a port that never crosses a boundary is not telling
the device anything at all.

## Doorbells

There are two encodings in the same register, and the difference is real, not
a typo to be "fixed":

| Direction | Value written to `HBUS_TARG_WRPTR` | Source |
|---|---|---|
| Transmit queue `q` | `write_ptr | (q << 8)` | `pcie/gen1_2/tx.c:118-121` |
| Receive queue `q` | `(q + 512) << 16` | `iwl-csr.h:557-558`, used at `pcie/gen1_2/rx.c:198` |

The receive encoding uses the constant `HBUS_TARG_WRPTR_Q_SHIFT`, the transmit
path does not.  Both work; this handbook records both and does not claim to
know why they differ.  A port that copies one and applies it to the other
direction will write a pointer the device reads as a different queue's.

## ASPM

The measured link state and the driver's behaviour are not the same question,
and both matter:

- The link advertises and has negotiated ASPM L1 (`LnkCap` / `LnkCtl` above).
- The driver forces **L0S off** in its own register, unconditionally, with a
  comment that L0S is unstable on these parts and unsupported on newer ones
  (`pcie/gen1_2/trans.c:209-214`).
- The driver deliberately does **not** disable L1 for this family: the block
  that would call `pci_disable_link_state()` is guarded by
  `pcie_l1_allowed`, which is true for `0x7000`
  (`pcie/gen1_2/trans.c:3908-3917`, `cfg/7000.c:58`).
- The device is told not to enter L1-active: `APMG_PCIDEV_STT_REG` gets
  `L1_ACT_DIS` during bring-up (`pcie/gen1_2/trans.c:308-310`).

So "ASPM L1 Enabled" in a PCI tool and "the device will not use L1-active" can
both be true at the same time: they are different states with confusingly
similar names.

## What this chapter cannot say

- **Electrical parameters.**  No differential swing, no de-emphasis setting
  this project chose, no reference-clock specification, no jitter or eye
  measurement, no lane polarity.  `lspci` reports the link's status bits; it
  does not measure margin.
- **Power.**  No power figure for the link or the part was measured here.
- **Why 31 queues.**  The number is declared by the family parameters.  How
  many of them the scheduler can use at once is in [`02-silicon.md`](02-silicon.md)
  and is not established either.
- **The meaning of the second doorbell encoding.**  Two encodings are
  observed; an explanation is not available in this tree.
- **Whether the device tolerates a different DMA address size.**  Only 36
  bits with a 32-bit fallback is exercised by this driver.
