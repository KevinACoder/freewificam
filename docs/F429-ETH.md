# STM32F429 wired Ethernet — LAN8720A (RMII), DHCP and the internet

Date: 2026-11-20.  Board: 野火 STM32F429 挑战者 V2 (STM32F429IGT6, 168 MHz,
1 MB flash, 192 KB SRAM + 64 KB CCM, 32 MB W9825G6 SDRAM on FMC bank 2).
Link: the board's RJ45 (HR911105A + **LAN8720A** PHY, RMII) into the lab
router, 100 Mbit/s full duplex, negotiated.

The point of this image is small and exact: put the F429 on the wired LAN with
the **vendor's** Ethernet driver (ST's STM32F4 HAL), take a DHCP lease from the
router, and answer the question "is the internet reachable" from the board
itself with ICMP echo replies.  It is the wired counterpart of the Wi-Fi data
plane, without a wireless stack and without a hand-written MAC driver.

## 1. The link, as the board carries it

| Item | Fact | Where it is from |
|---|---|---|
| PHY | LAN8720A-CP-TR (U16), RMII, PHY address **0** | V2 base schematic sheet 9; LAN8720A datasheet 3.7.1 (PHYAD0 default 0) |
| REF clock | PHY 25 MHz crystal (Y6) → PHY's REFCLKO → **PA1** (ETH_REF_CLK) 50 MHz | schematic sheet 9; REFCLKO is selected by nINTSEL strapped low (the LED nets' 4.7 kΩ pull-downs) |
| PHY reset | LAN8720A nRST is tied to the MCU **NRST**; there is no GPIO reset line on V2 | schematic sheet 9.  PI1 is the MPU6050 interrupt - the vendor V1 example's PI1 reset pulse is deliberately **not** reproduced |
| PHY interrupt | none: NINT is the REFCLKO pin | the same sheet |
| RMII pins (AF11) | PB11 TX_EN, PG13 TXD0, PG14 TXD1, PC4 RXD0, PC5 RXD1, PA7 CRS_DV, PA1 REF_CLK, PC1 MDC, PA2 MDIO | schematic sheet 9 |
| Pin conflicts | none in this image: FMC SDRAM owns PG0-5/8/15 and PC0 (not PG13/14, PC1/4/5), SDIO owns PC8-12/PD2, USB owns PB14/15, console owns PA9/10 | `platform/board/stm32f429-challenger/` |

The F429's ETH peripheral is the 10/100 MAC with its own DMA descriptor rings.
The HAL configures the MAC for the negotiated speed/duplex, enables the
store-and-forward FIFO modes (the F429 errata ES0227 §2.16.4/§2.16.5), and
leaves the descriptor cursors to this port.

## 2. What is imported, and what is written here

The MAC, the descriptor rings, MDIO and the PHY auto-negotiation are **ST's
STM32F4 HAL ETH driver, imported verbatim** from
`third-party/stm32f4xx-hal/` (HAL driver 1.7.3, CMSIS device 2.6.2 and core
4.30; 16 files byte-identical, `MANIFEST.sha256`; licence and closure rule in
that tree's `PROVENANCE.md`).  Only `stm32f4xx_hal_eth.c` is compiled; the
board's one RCC question (`HAL_RCC_GetHCLKFreq`, used for the MDC divider) is
answered by the board glue, and `HAL_GetTick`/`HAL_Delay` are the RTOS's -
FreeRTOS owns SysTick on this board, so the HAL must not install its own time
base.

Written here (all first-party, BSD-3-Clause):

| file | What it is |
|---|---|
| `platform/board/stm32f429-challenger/eth.c/.h` | the board seam: clocks and AF11 pin mux in the HAL's MSP hook, the HAL time base, the UID-derived MAC, the descriptor/buffer storage, the frame-level API `eth.h` publishes (read one frame / send one frame / link / stats / register snapshot), and one CMSIS-RTOS2 mutex around every HAL-handle call |
| `platform/board/stm32f429-challenger/stm32f4xx_hal_conf.h` | this image's HAL configuration: HSE 25 MHz, RCC + ETH modules, 8 + 8 rings of 1524 B, PHY address 0, 5 ms reset delay and 100 ms MDIO timeouts (the ST template's 0xFFFF ms would turn a dead MDIO bus into a 65 s hang) |
| `third-party/lwip/ports/stm32eth_netif.c/.h` | the lwIP netif bridge: `netif_add(..., tcpip_input)`, the transmit-side pbuf linearisation, the receive-side drain into `pbuf_alloc`/`pbuf_take`, and the bridge's own counters |
| `app/cases/f429_eth.c` | the ladder and the verdict: PHY identity, link, DHCP, ARP, gateway echo, name resolution (report only), public echo (the gate), counters, `PASS DATA` |

The receive side is **polled, not interrupt-driven**: `HAL_ETH_Init` is given
`ETH_RXPOLLING_MODE` (the HAL then does not enable the DMA interrupt), and a
4 KiB thread drains the 8-frame ring every millisecond.  At 100 Mbit/s that is
~12 KB/ms of ring capacity against a DHCP/ICMP load of a few frames per second;
the interrupt arm is an additive change, not a prerequisite.  Transmit runs on
the TCP/IP thread under the core lock, so the one HAL handle is shared by two
threads and the mutex is what keeps the HAL's non-reentrant cursor/lock state
consistent.

PHY register 31 is decoded the way the HAL's DP83848 masks happen to match the
LAN8720A: `HCDSPEED[4:2]` gives 001/101/010/110 = 10-half/10-full/100-half/
100-full, so bit 2 = 1 means 10 Mbit/s and bit 4 = 1 means full duplex.  The
case prints the decoded pair, so a 10 Mbit/s link is visible rather than
assumed.

## 3. Memory and image

| Region | Where | Why |
|---|---|---|
| FreeRTOS heap (256 KiB) | SDRAM `.sdram.heap` | the TCP/IP thread, the receive thread and the runner |
| lwIP heap (255 KiB) + the memp pools | SDRAM `.sdram.lwip` | the same plan as the other F429 lwIP images; the Cortex-M4 has no data cache |
| ETH descriptor rings + 8 + 8 station buffers (24.4 KiB) | internal SRAM `.bss` | the MAC's DMA must not go through the FMC, and the copy into lwIP's pbufs is the seam |
| lwIP pbufs | SDRAM heap | the driver copies into them; no DMA sees them |

The final image: **77,164 B** flash, 33,408 B static SRAM (of which 16 B
`.noncacheable`) and 524,288 B SDRAM; the link gate reports 163,200 B of RAM
margin.  Profile: `stm32f429-eth` (`-Os`, `CONFIG_CASES="f429_eth"`,
`F429_SDRAM`/`F429_HEAP_SDRAM`/`F429_LWIP_SDRAM`, `LWIP_DNS=y`).

```bash
cd freewificam
./make.rtos image PROFILE=stm32f429-eth
```

## 4. The ladder and its anchors

One boot runs the whole ladder; each rung prints one judged line:

| Rung | Anchor | What it proves |
|---|---|---|
| PHY/MAC up | `f429_eth: init rc=0` | the HAL's reset, PHY reset, link wait, auto-negotiation and MAC/DMA configuration completed |
| PHY identity | `f429_eth: phy id=0007:c0f1 addr=0` | MDIO answered; the part is the board's LAN8720A at its strap address |
| MAC | `f429_eth: mac=02:…` | a UID-derived locally-administered unicast address, stable per board |
| Link | `f429_eth: link speed=100 duplex=1` | the negotiated speed/duplex, decoded from BMSR + register 31 |
| DHCP | `f429_eth: dhcp ip=… mask=… gw=… lease_ms=…` | lwIP's DHCP client got a lease from the router |
| ARP | `f429_eth: arp gw=… mac=…` | the gateway's hardware address is in the table |
| Gateway echo | `f429_eth: ping dst=<gw> tx=10 rx=…` | report only (a router need not answer pings to itself) |
| Name (report) | `f429_eth: dns name=www.baidu.com rc=… ip=…` | the DHCP-provided resolver answered; not a gate |
| Internet | `f429_eth: ping dst=<public> tx=10 rx=…` | **the gate**: 223.5.5.5, 119.29.29.29, 8.8.8.8, 1.1.1.1 are tried in order until one answers 9/10 |
| Counters | `f429_eth: stats …` / `bridge …` | driver and bridge frame counts, so a failure names the layer |
| Verdict | `f429_eth: PASS DATA` | the ladder above, with the internet gate met |

## 5. Measured result (2/2 relay cold boots, same image)

| | boot 1 | boot 2 |
|---|---|---|
| image sha256 (BIN) | `2b508dba…` | `2b508dba…` |
| init | 1.46 s | 1.75 s |
| PHY / link | 0007:c0f1, 100 full | 0007:c0f1, 100 full |
| DHCP | 192.168.0.34/24, gw 192.168.0.1, 8.5 s | same lease, 9.7 s |
| gateway echo | 10/10, 3-4 ms | 10/10, 3-4 ms |
| name resolution | rc=0 (`183.240.99.224`) | rc=0 (`183.240.99.224`) |
| internet echo | 223.5.5.5 10/10, 31 ms | 223.5.5.5 10/10, 33 ms |
| driver counters | rx=100 tx=33, no errors | rx=103 tx=34, no errors |
| Pi → board (10 echoes) | 10/10 | 10/10 |
| Pi → public (5 echoes) | 5/5 | 5/5 |

The Pi (192.168.0.17, same L2) pings the board's lease and a public address
after each boot: that is the peer-side witness and the LAN's own internet
truth, independent of the board's counters.

Two observations, recorded rather than explained away:

* **The lease takes 8.5-9.7 s.**  A simultaneous tcpdump on the Pi shows two
  DHCP request packets from the board 25 ms apart - the DISCOVER and then the
  REQUEST, with the server's OFFER/ACK unicast to the board and invisible on
  the Pi - and no further board-side retransmission.  The wait is therefore in
  the server's reply path (or the board's reception of it), not in lwIP's
  retry loop.  The DHCP wait timeout in the case is 20 s, so this is reported, not
  gating.  A diagnostic image with lwIP's DHCP debug prints would name which
  side loses the time; it is not part of this round.
* **The gate names four public targets, not one.**  Public ICMP is
  policy-dependent (a target may rate-limit or stop answering echoes while
  still answering from other networks; one preliminary boot of this same
  code had 223.5.5.5 drop all ten while 119.29.29.29 answered ten), so the
  ladder tries 223.5.5.5, 119.29.29.29, 8.8.8.8 and 1.1.1.1 in order and
  passes on the first one at 9/10.  Both final boots were served by
  223.5.5.5.

## 6. Failure modes and what the anchors would say

| Symptom | Most likely cause | Distinguishing line |
|---|---|---|
| `init rc` non-zero and `dmabmr` bit 0 stuck | no 50 MHz REF_CLK (PA1 mux, PHY strap, PHY clock) | the PHY id line still reads if MDIO is alive |
| `phy id=ffff:ffff` or `0000:0000` | MDIO (PA2/PC1) wrong, PHY unpowered, or an address other than 0 | `macmiiar`/`macmiidr` in the `regs` line |
| `link speed=0` / init timeout in the link wait | cable, router port, or the PHY's auto-negotiation strap | BMSR/reg 31 in the `regs` line |
| `dhcp timeout` with `link speed=100` | DHCP/ARP path or the router | the `stats`/`bridge` counters and the driver's `missed` count |
| No public echo, gateway echo 10/10, peer ping 10/10 | upstream ICMP policy | the Pi's public ping in the same round |

## 7. Non-goals

Throughput (the polled 8-frame ring is built for correctness, not a
measurement; `docs/RK3568-PERFORMANCE.md`'s wired numbers are the other
board), IPv6, wake-on-LAN/PMT, the ETH interrupt arm, link-flap
renegotiation, the PHY interrupt (the pin is REFCLKO), and DNS as a gate.

## 8. References

* V2 base schematic, sheet 9 "以太网接口" (LAN8720A, HR911105A, straps):
  the vendor hardware zip, `F429_F767_H743II_挑战者_V2/原理图/`.
* LAN8720A datasheet §3.7.1 (PHYAD0, default 0), §3.7.4 (nINTSEL/REFCLKO) and
  §4.2.14 (register 31, HCDSPEED).
* STM32F429 reference manual, Ethernet chapter (MAC/DMA registers, descriptor
  format) and ES0227 §2.16.4/§2.16.5/§2.16.6/§2.16.11 (store-and-forward,
  back-to-back register writes, giant frames).
* The vendor lwIP example tree (`lwip_dhcp/User/eth/bsp_eth.c` and
  `User/arch/ethernetif.c`) is the sequence reference; its MAC address, its
  PI1 reset and its `sys_sem` serialisation are deliberately not carried over.
