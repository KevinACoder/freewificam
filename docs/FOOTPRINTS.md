# FOOTPRINTS

Resource footprint of the F429 images, measured 2026-11-16 with
`arm-none-eabi-size -A`, the link map and `tools/rtos/size-ledger.py`.
The tables are the image as linked (unless a row says "pre-link"); they are
not compiler estimates.

**Definitions.** FLASH is `.text` + `.rodata` + `.data` (the data's load
address is in flash). Static SRAM is `.data` + `.bss` + `.noncacheable` (the
data's run address is in SRAM). No-cache/DMA is the `.noncacheable` slice of
that SRAM; on the Cortex-M4 there is no data cache, so the section is ordinary
SRAM reserved for DMA staging. SDRAM is the `.sdram` NOLOAD section. A task
`hwm` from `uxTaskGetStackHighWaterMark()` is in **words**; bytes = `hwm * 4`.

## Image totals

| image | FLASH | static SRAM (incl. no-cache) | of which no-cache | SDRAM |
|---|---:|---:|---:|---:|
| `stm32f429-wifi-c6` delivery | 726,080 | 81,356 | 18,496 | 4,456,448 |
| `stm32f429-wifi-c6t` (throughput) | 735,936 | 81,484 | 18,496 | 4,456,448 |
| `stm32f429-wifi-c6` + `CONFIG_OPT_LTO` | 689,648 | 80,212 | 18,496 | 4,456,448 |
| `stm32f429-cfg80211-only` link probe | 168,036 | 92,660 | 16 | 1,048,576 |
| 8189FS driver, compile probe, delivery face (not linked) | 609,933 | 75,967 | 0 | — |

The cfg80211-only probe is a `tick` image and includes the tick case's 64 KiB
CPU buffers; it is a link probe, not a delivery profile. The 8189FS row is the
131-TU vendor driver compiled with the F429 face (`--gc-sections` has not run
yet); its base/lwIP/glue budget is in `BUILD.md`.

## `stm32f429-wifi-c6` by component

| component | FLASH | no-cache | RAM (data+bss) | SDRAM | heap |
|---|---:|---:|---:|---:|---|
| mac80211 | 254,499 | 0 | 449 | 0 | 0 |
| cfg80211 | 148,697 | 0 | 1,051 | 0 | 0 |
| rtl8xxxu (driver + firmware) | 113,738 | 0 | 2,619 | 0 | 0 |
| merged `rodata.str*` | 92,660 | 0 | 0 | 0 | 0 |
| liblinux80211 (kpi + bus + skb) | 41,359 | 18,440 | 30,723 | 0 | OS threads |
| lwIP | 35,080 | 0 | 3,517 | 0 | own 255 KiB heap |
| CherryUSB (core + hub + DWC2) | 12,918 | 0 | 20,896 | 0 | OS threads |
| FreeRTOS | 9,900 | 0 | 1,372 | 0 | 0 |
| platform | 5,547 | 0 | 844 | 4,194,304 | 0 |
| app/cases | 4,498 | 0 | 496 | 0 | runner stack |
| other / support / libc | 6,604 | 0 | 706 | 0 | 0 |
| **total** | **725,500** | **18,440** | **62,673** | **4,194,304** | ~105 KiB stacks |

The flash column is 580 B short of the 726,080 B bin (alignment/fill). Static
RAM is 62,673 B plus 187 B fill = 62,860 B; with the 18,496 B no-cache slice
the ELF's static SRAM is **81,356 B**. The SDRAM column is only the FreeRTOS
heap array; the lwIP reserve is the 262,144 B `.sdram` fill, so the section is
4,456,448 B. `Heap` is the FreeRTOS-heap allocation a component creates (the
thread stacks are counted in the task table, not repeated here); lwIP's
`MEM_SIZE` is its own heap in the reserve, not `pvPortMalloc`.

## The USB bus stack (pre-link, like a compile-size table)

These are per-object `size` numbers before `--gc-sections`; the image's placed
totals are the component table above. They answer "what does this file cost to
compile", not "what survives in the image".

| object | FLASH | no-cache | RAM | heap |
|---|---:|---:|---:|---:|
| `cherryusb/core/usbh_core.c` | 9,670 | 0 | 10,648 | 0 |
| `cherryusb/class/hub/usbh_hub.c` | 4,049 | 0 | 9,540 | 0 |
| `cherryusb/port/dwc2/usb_hc_dwc2.c` | 7,969 | 0 | 540 | 0 |
| `cherryusb/ports/osal/cmsis2/osal_cmsis2.c` | 841 | 0 | 168 | OS thread |
| `liblinux80211/bus/usb.c` (URB engine) | 6,649 | 2,056 | 4,187 | 0 |
| `liblinux80211/ports/usb-cherryusb/adapter.c` | 3,219 | 16,384 | 2,340 | 0 |
| `liblinux80211/skbuff/skbuff_pool.c` | 7,808 | 0 | 22,569 | 0 |
| `liblinux80211/linuxkpi_core.c` | 5,497 | 0 | 1,477 | 0 |

The two large no-cache blocks are the transfer stage pool (4 × 4096 B) and the
URB engine's coherent buffer; they are why the USB path's RAM is larger than
its flash. CherryUSB's placed share of the image is 12,918 B flash / 20,896 B
RAM; the liblinux80211 URB engine and skb pool are 41,359 B / 30,723 B / 18,440 B
no-cache as a group.

## OS threads (FreeRTOS heap)

Measured at the c6 case's task census. `hwm` is the FreeRTOS high-water mark
in **words of free stack** (the minimum remaining); free bytes = `hwm * 4`.

| task | stack (B) | hwm (words free) | free (B) at the census |
|---|---:|---:|---:|
| `lkpi_usb_drain` | 32,768 | 7,964 | 31,856 |
| `usb_tmr` | 4,096 | 985 | 3,940 |
| `Tmr Svc` | 2,048 | 470 | 1,880 |
| `IDLE` | 1,024 | 229 | 916 |
| `lkpi-bh` | 8,192 | 1,726 | 6,904 |
| `runner` | 32,768 | 6,818 | 27,272 |
| `usbh_hub0` | 8,192 | 1,628 | 6,512 |
| `lkpi-system` | 8,192 | 1,556 | 6,224 |
| `tcpip` (lwIP) | 8,192 | not sampled | — |
| **configured** | **105,472** | | |

The census runs before the data-plane pings, so these are **lower bounds on
headroom**, not a final peak: the drain thread's deepest path may be a TX
completion after this point. Read them as "the stack was at least this free at
association", and re-run a post-data census before shrinking any stack.

## SDRAM and heaps

The c6 image reserves 4,456,448 B in SDRAM: the 4 MiB FreeRTOS heap array
(4,194,304 B) plus the lwIP reserve (262,144 B = `CONFIG_LWIP_MEM_SIZE_KB` 255
KiB + 1 KiB for `mem.c`'s trailing records). The lwIP reserve is a linker
`--defsym`, not a second literal.

The case prints the FreeRTOS heap at its start anchor:
`heap=sdram free=4,157,952 min=4,157,952`. That point is **before** the USB
drain thread and the lwIP TCP/IP thread exist, so it is not a peak. The
configured thread stacks alone are 105,472 B; the 4 MiB heap is sized far
above the measured use. lwIP's own heap is 255 KiB, with `PBUF_POOL_SIZE=24`,
`TCP_WND`/`TCP_SND_BUF` 16 KiB and `MEMP_NUM_TCP_SEG=48` on the F429 branch.

## 8189FS compile probe (driver only)

The delivery face (`stm32f429-rtl8189fs-compile-trim`, 127/127 TUs) is 609,933 B
FLASH and 75,967 B static RAM (53,678 data + 22,289 bss), pre-link. The GT
face is 742,203 B / 80,191 B. The vendor stack allocates its adapter, receive
and transmit pools from the FreeRTOS heap at run time; that is not in a compile-probe
number and is not measured here (no SDIO host adapter in the tree yet). The
F429 profile gives that heap 4 MiB; the RK3568 8189 profiles use 16 MiB.

## Reproduce

```sh
# placed image
./make.rtos image PROFILE=stm32f429-wifi-c6
python3 tools/rtos/size-ledger.py --profile stm32f429-wifi-c6 \
  --elf tests/board/rtos/build/build/rtos-freertos.elf \
  --map tests/board/rtos/build/build/rtos-freertos.map

# compile objects
./make.rtos s1-driver-compile S1_PROFILE=stm32f429-rtl8189fs-compile-trim \
  S1_DRIVER=rtl8189fs S1_RUN=<n>
python3 tools/rtos/size-ledger.py --objects <run>/obj

# task high-water: the c6 case's `task[...] hwm=` lines on the console
```

## Caveats

- The c6 numbers are this board: STM32F429, 168 MHz, full-speed USB, 2.4 GHz
  HT20. They do not transfer to RK3568 or to a high-speed host.
- FLASH includes the `.data` load image; static SRAM includes the `.data`
  run image. No-cache is a subset, not extra.
- The pre-link USB table overstates the image: `--gc-sections` drops unused
  functions. The component table is the placed truth.
- The 8189FS row is a compiler probe; the SDIO host glue and the link are a
  separate line, so it has no placed flash/RAM yet.
- Heap is not attributed per module; the task table is the measured
  allocation, and the lwIP heap is separate by design.

## F429 UVC images (2026-11-20)

Measured with `arm-none-eabi-size -A` on the linked images. "Static SRAM" is
`.data` + `.bss` + `.noncacheable`, excluding the `.sdram` NOLOAD section;
"SDRAM" is the `.sdram` section (the FreeRTOS heap plus the camera frame pool).

| image | FLASH (bin) | text+data | static SRAM | SDRAM |
|---|---:|---:|---:|---:|
| `stm32f429-uvc-detect` | 58,836 | 58,824 | 107,260 | 0 |
| `stm32f429-uvc-480` | 69,644 | 69,632 | 50,188 | 1,441,792 |
| `stm32f429-uvc-800x600` | 69,652 | 69,640 | 50,188 | 1,441,792 |

The detect image keeps its 64 KiB FreeRTOS heap in SRAM and has no frame pool;
its larger static SRAM is the 16-altsetting USB device table. The two stream
images place a 1 MiB heap and three 128 KiB (480) or 512 KiB (800x600/720p)
frame buffers in SDRAM. The F429 RAM-margin gate still reports >80 KiB free
between `end` and `_estack` for the detect image and >140 KiB for the stream
images. The 1 MiB flash part is not a constraint for this line.
