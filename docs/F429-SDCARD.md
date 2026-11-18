# F429 TF-slot SD card line

The F429's TF socket as a **raw 512-byte block device**: the card is identified,
block 0 is read, one block in the card's last 1 MiB window is written, read
back, restored and compared.  This is a separate line from the 8189FTV SDIO
station: the socket, pins and controller are shared, the card type and stack
half are not.  It does not fix the 8189 line.

Measured on 2026-11-18 with a 29 GB SDHC card: **2/2 relay cold boots**,
`sdcard: PASS SDCARD` and `rtos: result pass cases=2/2`, image sha
`f5555386…` (49,784 B).

## Hardware

- TF socket on the SDIO bus the AP6181 shares: PC8-PC11 = D0-D3, PC12 = CLK,
  PD2 = CMD, AF12; no card-detect pin (the command path is the detector).
- PB13 is the AP6181's `WIFI_EN` and is held low; the 8189FTV module must be
  removed and a plain SD card inserted for this line.
- The STM32F429 SDIO peripheral runs at 24 MHz (48 MHz source / 2), polled PIO;
  no DMA in this line.

## Stack and port

- `third-party/fsl_sdmmc/sd/fsl_sd.c` is the imported SD card stack (BSD-3),
  consumed without edits.
- `third-party/fsl_sdmmc/ports/sdmmc_sd_card.c` is the first-party instance:
  `SD_CfgInitialize`, `sdmmc_sd_card_start/stop`, the info/id getters and the
  block read/write wrappers.
- `third-party/fsl_sdmmc/ports/sdmmc_host_stm32f4_sdio.c` is the same host glue
  the 8189FTV line uses, extended for the SD card half.
- Profile `stm32f429-sdcard`: `tick f429_sd_card`, 80 KiB FreeRTOS heap in
  SRAM, 32 KiB runner stack, PIO, 24 MHz.

## Host-glue adaptations this line needed

All five were found by the line's own boots; none is in the imported stack.

1. **R2 word order.**  The imported decoders number the four R2 words from the
   least significant end (`SD_DecodeCid` reads the manufacturer from
   `response[3]`, `SD_DecodeCsd` reads CSD[127:126] from `rawCsd[3]`).  The ST
   controller's RESP1 is the **most** significant word.  The host fills
   RESP4/RESP3/RESP2/RESP1 into `response[0..3]`.  Before the fix the card
   decoded as a plausible 448 KB/SD 3.0 card (CSD size 3585, `read_bl_len=3`)
   and the geometry gate stopped the case; after it the CSD is SDHC v2.0,
   C_SIZE 59635, 61,067,264 blocks.
2. **ACMD41's R3 has no CRC.**  The controller reports `CCRCFAIL` on it; the
   host treats R3 like R4 (ignore the CRC flag and use the response).  Without
   this `SD_Init` stops at the operation-condition handshake.
3. **SCR and SD status are MSB-first.**  The stack asks the host to convert
   wide-register packets; the F4 FIFO hands the first byte back in the low byte
   of the first word, so the host swaps bytes within each 32-bit word for
   `kSDMMC_DataPacketFormatMSBFirst` only.  CMD6's switch status is usual
   LSB-first data and is not converted.  The SCR then reports `bus_widths=0x05`
   (1-bit and 4-bit), and the stack switches to four data lines.
4. **DAT0 busy.**  The host answers `cardIsBusy` from PC8/DAT0 so the stack's
   `SD_PollingCardStatusBusy` runs its timeout instead of issuing CMD13 without
   a bound.
5. **Block-write FIFO fill.**  A 512-byte block lasts about 43 µs at 24 MHz
   four-bit.  A per-word helper loop underran after 128 bytes; the ST
   reference's half-empty eight-word bursts underran at 448/512 and 440/512;
   a no-prefill half-empty variant underran at 32/512.  The delivery shape
   prefills the 32-word FIFO, then, for the rest of the block, fills every free
   FIFO slot with direct stores while the CPU's interrupts are held off, and
   waits for the card's `DBCKEND`/`DATAEND`.  The restore path uses the same
   shape.

## Gates and measured serial anchors

```
sdcard: cardsize blocks=61067264 block_size=512 clock=24000000 width=4 timing=1 version=8 flags=0x000000ce
sdcard: cid manufacturer=0xfe oem=0x3432 product=SD rev=0x20 serial=0x0000008b date=0x019b
sdcard: csd structure=1 size=59635 read_bl_len=9
sdcard: scr spec=2 bus_widths=0x05
sdcard: read lba=0 rc=0 bytes=512 crc=0x55a8dee9
sdcard: rw lba=61065216 crc_w=0xf54345a2 crc_r=0xf54345a2 verify=ok
sdcard: restore lba=61065216 verify=ok
sdcard: PASS SDCARD
```

The write window is `blocks - 2048` (the last 1 MiB); the case reads the
original 512 bytes, writes a position-dependent pattern, reads it back,
restores the original and compares.  The LBA is printed.  The bench still uses
a scratch card: a power cut between the write and the restore leaves the
pattern in that one block.

The bench card reports a non-standard CID (`0xfe`/`0x3432`, product `SD`); its
CSD is the credible SDHC v2 geometry and the read/write gates pass.  The CID is
reported as read, not used to claim a brand.

## Boundaries

- Single-block transfers only.  Multi-block CMD18/25 is the next arm; the host
  glue's completion wait still accepts `DBCKEND`, which a multi-block write can
  raise before the last block, so that wait must be fixed first.
- Raw blocks, not FatFs; no partition parser, no file system.
- PIO, 24 MHz, four-bit, no DMA, no card-detect/hotplug, no power control.
- The 8189FTV vendor-stack line remains blocked at the scan gate; this line
  validates the socket/pins/clocks and the host glue, not that stack.

## Reference

The vendor's `A-2-1-SDIO-SD卡-基础验证` example (pin map, 1-bit to 4-bit, 512-byte
block read/write, 32 GB note) is the reference for the socket and the card
sequence; the implementation stays on this repo's first-party ST LL/register
path.  Its `A-2-2` FatFs example is the file-system follow-up, not this line.
