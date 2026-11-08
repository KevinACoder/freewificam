# 02 - Silicon, as the driver sees it

This chapter is about the part below the 802.11 layer: how it is brought out
of reset, which clock and power requests the host has to make, and which
register blocks implement them.  Every statement here comes from the driver,
because there is no public datasheet to check against.

**Read the block diagram the right way.**  The figure below is the set of
windows the driver touches, drawn as a hierarchy because access is
hierarchical.  It is not a floor plan, and the boxes are not functional units
in the vendor's sense - they are the drivers' names for address ranges.

## The access surface

```
host
  |
  +-- PCIe configuration space                    -- 03-bus.md
  |
  +-- BAR0, 8 KiB, memory mapped
  |     |
  |     +-- CSR block          0x000 .. 0x3ff     reset, clock, interrupt, mailbox
  |     |     (iwl-csr.h:29-46)
  |     |
  |     +-- HBUS block         0x400 .. 0x5ff     indirect access to PRPH
  |     |     (iwl-csr.h:512-558)
  |     |
  |     +-- FH window          0x1000 .. 0x2000   TX/RX DMA rings, doorbell
  |           (iwl-fh.h:22-23)
  |
  +-- PRPH address space       0x00000 .. 0xfffff  reached only through HBUS
        (iwl-prph.h:15-16)
        |
        +-- APMG               0x03000 + offset   clocks, power state, RFKILL
        |     (iwl-prph.h:19-45)
        |
        +-- SCD                0xa02c00 + offset  TX scheduler state
        |     (iwl-prph.h:223-235)
        |
        +-- OSC_CLK            0xa04068           oscillator force control
        |     (iwl-prph.h:240-241)
        |
        +-- CPU status words   0xa01e30, 0xa01e34
              (iwl-prph.h:401-402)
```

Two access paths are worth stating separately because they explain most of the
odd-looking code:

- **PRPH is not memory mapped.**  A write is two BAR0 stores: the address into
  `HBUS_TARG_PRPH_WADDR`, the value into `HBUS_TARG_PRPH_WDAT`
  (`pcie/gen1_2/trans.c:1897-1904`).  A read is an address store followed by a
  read of `HBUS_TARG_PRPH_RDAT` (`pcie/gen1_2/trans.c:1888-1895`).  The
  address carries a `(3 << 24)` marker; the address mask is `0x000fffff` for
  this family (`pcie/gen1_2/trans.c:1880-1886`).
- **SHR registers are a third path.**  They go through two more BAR0 words,
  `HEEP_CTRL_WRD_PCIEX_CTRL_REG` and `HEEP_CTRL_WRD_PCIEX_DATA_REG`
  (`pcie/gen1_2/trans.c:171-183`, register offsets `iwl-csr.h:494-495`), with
  a command code in the top nibble.  Only the low-power-crystal path in this
  chapter uses them.

## Reset and ownership

### The control register

Almost every handshake in this chapter is a bit in `CSR_GP_CNTRL`
(offset `0x024`, `iwl-csr.h:36`).  The bits the driver uses, with their
declared meanings:

| Bit | Name | Direction | What the driver does with it |
|---|---|---|---|
| 0 | `MAC_CLOCK_READY` | device sets | polled while waking the NIC (`pcie/gen1_2/trans.c:4430`) and while taking ownership (`pcie/gen1_2/trans.c:2377`) |
| 2 | `INIT_DONE` | host writes | set to move the part from D0U to D0A (`pcie/gen1_2/trans.c:4418`), cleared to move it back (`pcie/gen1_2/trans.c:396`) |
| 3 | `MAC_ACCESS_REQ` | host writes | request ownership of the MAC registers (`pcie/gen1_2/trans.c:2353`) |
| 4 | `GOING_TO_SLEEP` | device sets | part of the ownership poll mask: a set bit means the part is on its way down (`pcie/gen1_2/trans.c:2334-2335`) |
| 10 | `XTAL_ON` | host writes | force the crystal on during the low-power-crystal sequence (`pcie/gen1_2/trans.c:337-338`) |
| 29 | `BUS_MASTER_DISABLE_REQ` | host writes | newer families only; this part uses `CSR_RESET` instead |
| 31 | `SW_RESET` | host writes | software reset (`pcie/gen1_2/trans.c:82-83`) |

The whole group is declared at `iwl-csr.h:287-306`.  The two directions are
not interchangeable: a poll that reads a host-written bit proves nothing, which
is why the wake path polls `MAC_CLOCK_READY` and the ownership path polls
`MAC_CLOCK_READY` and `GOING_TO_SLEEP` together.

### Ownership: who is allowed to touch the registers

Before any register that is not a plain status read, the driver takes
ownership: it sets `MAC_ACCESS_REQ` and then polls for `MAC_CLOCK_READY` set
with `GOING_TO_SLEEP` clear, with a 15 ms budget
(`pcie/gen1_2/trans.c:2352-2377`).  A timeout is not silently ignored - the
driver dumps the register set, and then either removes the device or forces an
NMI into the firmware (`pcie/gen1_2/trans.c:2378-2397`).  The comment above
the poll explains what the two bits mean physically: the part is running and
will keep running while the request bit is held, but the bits do not say that
internal SRAM has been restored, and 5000-series-and-later SRAM is
non-volatile so no restore is needed (`pcie/gen1_2/trans.c:2357-2376`).

This handshake is the answer to "why does a register read fail after resume":
the read itself may be fine, but anything that requires the MAC domain needs
ownership first.

### Bring-up: the APM init order

`iwl_pcie_apm_init()` (`pcie/gen1_2/trans.c:231-320`) is the sequence that
turns a reset part into one whose registers and DMA are usable.  In order:

| Step | Register | Action | Driver's stated reason |
|---|---|---|---|
| 1 | `CSR_GIO_CHICKEN_BITS` | set `DIS_L0S_EXIT_TIMER` (this family only) | "platform NMI Work/Around" (`pcie/gen1_2/trans.c:242-245`) |
| 2 | `CSR_GIO_CHICKEN_BITS` | set `L1A_NO_L0S_RX` | "don't wait for ICH L0s (ICH bug W/A)" (`pcie/gen1_2/trans.c:247-252`) |
| 3 | `CSR_DBG_HPET_MEM_REG` | write `0xffff0000` | "Set FH wait threshold to maximum (HW error during stress W/A)" (`pcie/gen1_2/trans.c:254-255`) |
| 4 | `CSR_HW_IF_CONFIG_REG` | set `HAP_WAKE` | "enable HAP INTA ... to wake device's PCI Express link L1a -> L0s" (`pcie/gen1_2/trans.c:257-262`) |
| 5 | - | call `iwl_pcie_apm_config()` | link state and L0S policy, next section |
| 6 | `CSR_ANA_PLL_CFG` | set `0x00880300`, **but not on this part**: the write is guarded by a family flag (`pcie/gen1_2/trans.c:266-268`) that only the 1000 and 5000 configurations set (`cfg/1000.c:36`, `cfg/5000.c:37`), so the 7000 base leaves it null | "configure analog phase-lock-loop before activating to D0A"; value `iwl-csr.h:459` |
| 7 | - | `iwl_trans_activate_nic()` | INIT_DONE and the clock-ready poll, below |
| 8 | `OSC_CLK` | read, read, set bit 3, read, read | 7260/3160 only; see the clock section |
| 9 | `APMG_CLK_EN_REG` | request the DMA clock, then `udelay(20)` | "enable DMA clock and wait for it to stabilize" (`pcie/gen1_2/trans.c:296-306`) |
| 10 | `APMG_PCIDEV_STT_REG` | set `L1_ACT_DIS` | "disable L1-Active" (`pcie/gen1_2/trans.c:308-310`) |
| 11 | `APMG_RTC_INT_STT_REG` | write `RFKILL` bit | "clear the interrupt in APMG if the NIC is in RFKILL" (`pcie/gen1_2/trans.c:312-314`) |

Two style points matter for a reader who wants to reimplement this.  Every
step that says `set` uses a read-modify-write to preserve reset defaults the
driver does not know about, and the comment says so
(`pcie/gen1_2/trans.c:237-240`).  And step 7 is where the driver stops
treating the part as a PCI device and starts treating it as a processor
complex: `iwl_pcie_gen1_2_activate_nic()` sets `INIT_DONE` and then polls
`MAC_CLOCK_READY` for up to 25 ms, and if that fails it dumps the host monitor
registers and returns an error (`pcie/gen1_2/trans.c:4411-4435`).

### Teardown: stop master, then stop

The stop path is asymmetric with the start path, and the asymmetry is
deliberate.

1. `iwl_pcie_apm_stop_master()` stops bus-master DMA.  For this family it
   writes `STOP_MASTER` into `CSR_RESET` and polls `MASTER_DISABLED` for
   100 us; the newer families use the `CSR_GP_CNTRL` request/status pair
   instead (`pcie/gen1_2/trans.c:413-438`, flags at `iwl-csr.h:244-245`).
2. `iwl_pcie_apm_stop()` then does one of two things
   (`pcie/gen1_2/trans.c:440-483`):
   - if the op mode is leaving, it tells the management engine by setting
     `WAKE_ME` for this family (`pcie/gen1_2/trans.c:449-451`);
   - if the configuration sets `lp_xtal_workaround` (it does for this part,
     `cfg/7000.c:136`), it runs the low-power-crystal sequence and **returns
     early**, without clearing `INIT_DONE` (`pcie/gen1_2/trans.c:471-474`).

So on this card the "powered down" state is not the same as the state after a
plain reset, and the next bring-up has to be written for that.  What the
low-power state is electrically is not established here.

## Clocks

The driver never programs a frequency.  There is no PLL divisor, no reference
clock value and no measurement of any clock in this tree; what exists is
permission to use clocks and one workaround that keeps an oscillator running.

| Request | Register | What it does | Where |
|---|---|---|---|
| DMA clock | `APMG_CLK_EN_REG` bit 9 | request the DMA clock, then wait 20 us | `pcie/gen1_2/trans.c:304-306`, value `iwl-prph.h:32` |
| Oscillator, forced on | `OSC_CLK` bit 3 | keeps the oscillator counting wake-up time for L1 exit; the driver's comment puts the cost at about 100 uA | `pcie/gen1_2/trans.c:274-293` |
| Crystal on | `CSR_GP_CNTRL` bit 10 | held during the low-power-crystal sequence | `pcie/gen1_2/trans.c:337-338` |
| Low-power crystal | three SHR registers | enable, then release, the low-power crystal | `pcie/gen1_2/trans.c:352-410` |

The `OSC_CLK` sequence is worth quoting, because it is the one place where the
driver admits it does not know why:

```c
		/*
		 * This looks weird: read twice the same register, discard the
		 * value, set a bit, and yet again, read that same register
		 * just to discard the value. But that's the way the hardware
		 * seems to like it.
		 */
		iwl_read_prph(trans, OSC_CLK);
		iwl_read_prph(trans, OSC_CLK);
		iwl_set_bits_prph(trans, OSC_CLK, OSC_CLK_FORCE_CONTROL);
		iwl_read_prph(trans, OSC_CLK);
		iwl_read_prph(trans, OSC_CLK);
```

That comment is the honest state of knowledge: the sequence is required, the
reason is not documented.  A port that "cleans it up" to a single
read-modify-write is changing an undocumented timing property, and this
handbook cannot say what breaks.

The low-power-crystal path exists because of a stated power bug: after a
device reset with no firmware loaded, the part can consume much more power
unless the low-power crystal is enabled, and it is disabled by default after a
hardware reset (`pcie/gen1_2/trans.c:322-328`).  It also sets a persistence
bit so a shared hardware reset in S3 does not undo the work
(`pcie/gen1_2/trans.c:352-357`, `pcie/gen1_2/trans.c:385-390`).  All three SHR
registers it touches are listed at `iwl-prph.h:64-78`.

## Power

The APMG block is where the part's power state is requested and reported.
Its registers, and what this driver does with each:

| Offset | Register | Driver use |
|---|---|---|
| `0x000` | `APMG_CLK_CTRL_REG` | read only, in a WiMAX-coex workaround that does not apply to this part (no `internal_wimax_coex`) |
| `0x004` | `APMG_CLK_EN_REG` | DMA clock request, step 9 above |
| `0x008` | `APMG_CLK_DIS_REG` | written when the bus-master clocks are powered down (`pcie/gen1_2/trans.c:1216-1221`) |
| `0x00c` | `APMG_PS_CTRL_REG` | power-source select for D3cold, and one reset-state workaround |
| `0x010` | `APMG_PCIDEV_STT_REG` | L1-active disable, persistence, wake-me |
| `0x01c` | `APMG_RTC_INT_STT_REG` | clear the RFKILL interrupt |
| `0x058` | `APMG_DIGITAL_SVR_REG` | defined, and written only by the DVM op mode (`dvm/devices.c:49-51`); not written on this path |
| `0x06c` | `APMG_ANALOG_SVR_REG` | defined, not written anywhere in this tree |

Three of those deserve a sentence each:

- **Power source.**  `iwl_pcie_set_pwr(vaux)` selects between main power and
  auxiliary power in `APMG_PS_CTRL_REG`, and only takes the auxiliary path if
  the platform reports D3cold capability (`pcie/gen1_2/trans.c:185-198`).
  The bit field and its two values are `iwl-prph.h:37-39`.
- **Early power-off reset state.**  The driver clears
  `APMG_PS_CTRL_EARLY_PWR_OFF_RESET_DIS` at init
  (`mvm/ops.c:135-143`); its comment describes the failure it prevents: if
  PCIe power is lost before PERST# is asserted, the NIC can stay stuck in
  reset and the management engine loses ownership.  That is a hardware
  erratum with a workaround, recorded here and again in
  [`07-errata.md`](07-errata.md).
- **Voltage configuration.**  `APMG_SVR_DIGITAL_VOLTAGE_1_32` is defined as a
  field value (`iwl-prph.h:40-41`) and used by the DVM op mode.  What voltage
  the encoding corresponds to, and what this part is actually fed, is **not
  established**: the field name says 1.32 V, the driver does not say what it
  selects, and nothing in this tree measures a supply.

## The scheduler block

`SCD` is the driver's name for the transmit scheduler window at
`0xa02c00` (`iwl-prph.h:223`).  The driver's use is narrow and mechanical:

| Operation | Register | Effect |
|---|---|---|
| chain a queue | `SCD_QUEUECHAIN_SEL` | set the queue's bit (`iwl-scd.h:13-17`) |
| enable aggregation on a queue | `SCD_AGGR_SEL` | set the queue's bit (`iwl-scd.h:19-23`) |
| disable aggregation on a queue | `SCD_AGGR_SEL` | clear the queue's bit (`iwl-scd.h:25-29`) |
| activate FIFOs | `SCD_TXFACT` | write a mask of FIFOs 0 to 7 (`iwl-scd.h:36-39`) |
| deactivate FIFOs | `SCD_TXFACT` | write zero (`iwl-scd.h:41-44`) |
| scheduler enable | `SCD_EN_CTRL` | write a control word (`iwl-scd.h:46-50`) |
| read/write pointer per queue | `SCD_QUEUE_WRPTR()` / `SCD_QUEUE_RDPTR()` | computed addresses, two different layouts for queues below and above 20 (`iwl-scd.h:52-66`) |

Register offsets: `iwl-prph.h:228-235`.  What the scheduler does internally -
how it arbitrates between the FIFOs, what its internal queue states are, what
the write/read pointers mean beyond "producer and consumer" - is **not
established**.  The driver only ever turns things on and off and reads
pointers.

## What this chapter cannot say

- **Clock frequencies.**  No crystal frequency, no PLL multiplier, no bus
  clock, no core clock.  The driver programs none of them and this project has
  measured none.
- **Supply voltages and power domains.**  One field name mentions 1.32 V; that
  is a driver constant, not a measurement, and it is not even written on this
  path.
- **The die's internal blocks.**  The figure at the top is an address map.
  Nothing here establishes what is inside the part, how many processors it
  has, or how the blocks connect.
- **Reset domain boundaries.**  The driver runs one reset sequence.  Whether
  the crystal, the MAC domain and the DMA domain reset independently is not
  recorded anywhere it can be checked.
- **What the APMG and SCD actually implement.**  Their register interfaces are
  described; their state machines are not.
- **Why the oscillator sequence needs two reads.**  The driver's comment is
  reproduced; an explanation is not available.
