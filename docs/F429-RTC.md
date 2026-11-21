# STM32F429 RTC — the battery-backed calendar and the system date/time

Date: 2026-11-21.  Board: 野火 STM32F429 挑战者 V2 (STM32F429IGT6, 168 MHz,
1 MB flash, 192 KB SRAM).  Profile `stm32f429-rtc`, case `rtc`.

The platform had no wall clock.  This line adds one: the chip's RTC running
from the core board's 32.768 kHz crystal, a first-party register driver, and a
small system-time service (`rtos_time_*`) that gives the image an epoch, a
calendar, UTC/local formatting and, through newlib's `_gettimeofday` seam,
a working `time()`.  The vendor tutorial's RTC examples were read as a
reference for the register sequence; no vendor driver or HAL file is compiled
into this image, and the board layer remains first-party register code.

## 1. The hardware the driver is written for

| Item | Fact |
|---|---|
| LSE crystal | **Y2 32.768 kHz** on the core board, `OSC32_IN/OSC32_OUT`, C29/C30 10 pF |
| Battery | **CR1220 holder** on the base board, `VBAT`; the core board switches battery/3V3 through a BAT54C |
| RTC source | LSE preferred; LSI fallback (the LSI is imprecise and is not an acceptance source) |
| 1 Hz from LSE | `PREDIV_A = 127`, `PREDIV_S = 255`; `32768 / (128 * 256) = 1 Hz` |
| Fallback | `PREDIV_A = 127`, `PREDIV_S = 249` for the LSI's 32 kHz nominal |
| Calendar | BCD, 24-hour, year 2000-2099 (the RTC's two-digit year) |

The battery is optional.  With it fitted the RTC domain survives the relay
power cut; without it a cold boot starts from an empty domain.  The driver
does not require the battery to run; it only changes what survives a reset.

## 2. The driver: `platform/board/stm32f429-challenger/rtc.c`

`rtc_board_init()` runs from `main()` before the scheduler and is idempotent:

1. `PWREN`, `DBP`; read `RCC_BDCR`.
2. If `RTCEN` is set and `RTCSEL` names LSE or LSI, keep that source (the
   source can only be changed by a backup-domain reset).  Confirm the
   oscillator and use the stored calendar.
3. Otherwise configure a source: LSE at the default drive for 1.5 s, then at
   high drive for another 1.5 s; if it still does not start, LSI.  Set
   `RTCSEL`, `RTCEN`, wait for `RSF`, then program `PRER` and the 24-hour
   format in init mode under `WPR`.
4. Reject a 12-hour `CR.FMT` instead of misreading it.
5. Increment the boot counter in the RTC domain (used as evidence, not a gate).

A foreign calendar (a magic this firmware did not write) is handled by
`CONFIG_F429_RTC_TAKE_OWNERSHIP`: the first boot resets the RTC domain once,
selects LSE and writes this firmware's magic.  With the option off the foreign
calendar and its source are preserved, and the acceptance case reports a
non-LSE source rather than pretending.  This exists because a development
board can arrive with a vendor demo's calendar already running; the magic is
`0x52544331` ("RTC1"), deliberately not the vendor example's `0x32F2`.

### 2.1 The date write latches at the next calendar update

The first version wrote `TR` and `DR` in one init session.  On the bench board
the time took and the date did not; the readback 65 ms after the write still
showed the old date, while 2.2 s later the written date was there.  The cause
is the DR latch: unlike TR, the date register is applied at the next calendar
update, not at the `INIT` exit.  The driver now:

1. writes `DR` in its own init session and waits for the date fields to read
   back (poll, up to 1.5 s);
2. writes `TR` in a second session, so the time is exact;
3. verifies the whole calendar through the shadow registers and returns a
   named negative code if it still disagrees.

A set that changes the date therefore takes up to about two seconds on the
first attempt (the case retries once); a set whose date is already current
returns in about 10 ms.  The acceptance case's retry loop covers the one
legitimate disagreement, a second boundary crossed between the write and the
read.

### 2.2 RTC-domain schema

| Register | Meaning |
|---|---|
| DR0 | magic `0x52544331` ("RTC1"): this firmware configured the calendar |
| DR1 | boot counter, once per `rtc_board_init()` |
| DR2 | epoch of the last successful set (written by the time service) |

The boot counter is the reset-class witness: a pin reset increments it, a
cold boot with the battery increments it, and a cold boot without the battery
starts it at 1 again.

## 3. The system time service: `app/rtos_time.c`

| Call | What it does |
|---|---|
| `rtos_time_init()` | brings the RTC up; writes the configured default only when the magic is absent or the calendar invalid |
| `rtos_time_epoch()` | current Unix epoch, 0 when not readable |
| `rtos_time_set_epoch()` | writes the RTC, DR2 and the magic |
| `rtos_time_get_tm()` / `rtos_time_set_tm()` | calendar in UTC or in the configured fixed offset |
| `rtos_time_format_utc()` / `rtos_time_format_local()` | `YYYY-MM-DD HH:MM:SS` (local adds `+HHMM`) |
| `rtos_time_epoch_to_tm()` / `rtos_time_tm_to_epoch()` | pure conversions, also used by the case's table |
| `rtos_time_info_get()` | source, boot counter, last set, default, offset |

Conventions: the RTC holds UTC; local time is a fixed offset
(`CONFIG_F429_RTC_TZ_OFFSET_MIN`, default +480), no daylight saving.  The
epoch is 32-bit and covers 2000-01-01 through 2099-12-31; the conversion
helpers also accept 1970 for tests.  newlib's `time_t` on 32-bit ARM is signed
32-bit, so `time()` itself ends in 2038 even though this API reaches 2099 -
documented, not worked around in this round.

`CONFIG_F429_RTC_POSIX_TIME` defines `_gettimeofday`; the image links
`-lnosys`, whose stub would otherwise claim the symbol, and the strong
definition in the image's objects wins.  The seam is seconds resolution.

The configured default (`CONFIG_F429_RTC_DEFAULT_EPOCH`, default
`1767225600` = 2026-01-01 00:00:00 UTC) is written only on a first boot; an
existing, valid calendar is never rewritten at boot.  The acceptance image's
case does write a known time later, by design.

## 4. Boot line and case anchors

Every image with `CONFIG_F429_RTC=y` prints one line after the banner:

```text
rtos: time 2026-01-01 00:00:00 UTC epoch=1767225600 src=lse boot=1 first=1 local=2026-01-01 08:00:00 +0800
```

The case prints one line per rung; the acceptance boot's transcript is in
[CONSOLE-EVIDENCE.md](CONSOLE-EVIDENCE.md).  The lines the judge reads:

```text
rtc: init rc=0 src=lse lse=1 lsi=0 boot=1 configured=0 owned=0
rtc: regs bdcr=00008103 csr=0e000000 cr=00000000 tr=00000000 dr=00268101 isr=00000037 prer=007f00ff
rtc: entry 2026-01-01 00:00:00 UTC epoch=1767225600 wday=4 local=2026-01-01 08:00:00 +0800
rtc: persist boot=1 skip=first-boot
rtc: set 2026-01-02 03:04:05 UTC epoch=1767323045 want=1767323045 wday=5 rc=0
rtc: readback 2026-01-02 03:04:05 UTC epoch=1767323045 wday=5 ok=1
rtc: convert cases=8 fail=0
rtc: advance t0=1767323045 t1=1767323047 delta=2 ok=1
rtc: posix time=1767323047 epoch=1767323047 ok=1
rtc: PASS
```

The case gates the source (LSE, not the LSI fallback), the raw `BDCR`/`PRER`
values, the date<->epoch arithmetic against eight known rows (including leap
days, 2038-01-19 and 2099-12-31), the exact set/readback pair, the second
counter's advance and `time()`.  Persistence is printed but not gated inside
the case: a first boot has nothing to compare against, and a battery-backed
board can enter the first boot with an older calendar.  The run-level judge
asks for `rtc_persist=required` on the pin-reset boot only.

The `owned=1` value appears when the boot reset a foreign RTC domain; the
acceptance below starts from a deliberately cleared domain, so its first boot
reports `owned=0`.

## 5. Acceptance

Profile `stm32f429-rtc` (case list `rtc`).  The first boot of the round was
preceded by a pyOCD `RCC_BDCR.BDRST` pulse so the RTC domain was empty; the
same image then ran:

```bash
cd freewificam
./make.rtos image PROFILE=stm32f429-rtc
# bench: flash the BIN, then
#   boot 1: RTC domain cleared, relay cold boot
#   boot 2: pyOCD reset (SYSRESETREQ), no reflash
#   boot 3: relay cold boot, same image, no reflash
python3 tools/rtos/judge.py <serial.txt> --expect cases=rtc            # boot 1/3
python3 tools/rtos/judge.py <serial.txt> --expect cases=rtc \
        --expect rtc_boot_min=2 --expect rtc_persist=required          # boot 2
```

| | boot 1 | boot 2 | boot 3 |
|---|---|---|---|
| reset | relay cold boot, domain cleared | pyOCD reset | relay cold boot |
| boot counter | 1 | 2 | 3 |
| source | LSE, `PRER=0x007F00FF` | LSE | LSE |
| entry | 2026-01-01 00:00:00 UTC | 2026-01-02 03:04:42 UTC | 2026-01-02 03:04:36 UTC |
| persist | `skip=first-boot` | `last=1767323045 now=1767323082 dt=37 ok=1` | `last=1767323045 now=1767323076 dt=31 ok=1` |
| set/readback | exact | exact | exact |
| advance | 2 s in 2.2 s | 2 s | 2 s |
| `time()` | matches | matches | matches |
| case | PASS | PASS | PASS |

Image: BIN sha256 `2882f0349b3a33b045553f15e8b6170979be0bb4c4a26b2434d5274bb054859c`,
30,856 B; build tag `c963821`; link gate `_start=0x08000000`, 0 undefined,
RAM margin 128,576 B.  The same digest is recorded in all three boots.

The boot counter continuing across the relay cold boot (2 -> 3) is the
measured proof that the CR1220 is fitted and the RTC domain survives the power
cut.  Without the battery boot 3 would report `boot=1 configured=0` and still
pass as a first boot; the difference is the battery, not the driver.

## 6. Failure modes and what the lines say

| Symptom | Most likely cause | Distinguishing line |
|---|---|---|
| `src=lsi lse=0`, case `FAIL reason=src-not-lse` | LSE did not start (crystal, load caps, drive) or the domain was configured for LSI | `rtc: regs` shows `BDCR` without `RTCSEL=01`/`LSERDY`; the case does not pretend LSI is the acceptance source |
| `rtc: init rc=-1`, `rtos: time unavailable` | no source could be started | the boot line, then the case's `no-time` |
| `configured=1` on an unexpected first boot | the RTC domain survived a reset (battery, or a pin reset) | `boot=` and `rtos: time ... first=0` |
| `persist ok=0` on the pin-reset boot | the domain was lost or the last-set epoch is stale | the `persist` line's `last/now/dt` |
| `set rc=-3` | the readback disagreed (usually a second boundary) | the case retries; a persistent -3 is the finding |
| `set rc=-4` | the date never latched in 1.5 s | the case's `rtc: set` line |
| `posix ok=0` | `_gettimeofday` was not linked or the clock is not ready | `rtc: posix` |

## 7. What this round does not do

- No SNTP or network time.  `rtos_time_set_epoch()` is the hook for a later
  `LWIP_SNTP` + `SNTP_SET_SYSTEM_TIME` arm; the RTC is the offline source.
- No FAT filesystem timestamps (this tree has no FatFs).
- The console stamp stays boot-relative; replacing it with wall-clock stamps
  is a separate, evidence-format decision.
- No alarm/wakeup/tamper/calibration, no sub-second wall clock, no 12-hour
  format, no timezone database (fixed offset only).
- The LSI fallback is a fallback: its frequency is imprecise, so its calendar
  drifts and it is not an acceptance source.
- One writer is assumed (main and the runner's case); a future SNTP client
  must serialise at the `rtos_time` layer.
- newlib's 32-bit `time_t` ends in 2038; `rtos_time_*` reaches 2099.

## 8. Reproduce the host-side arithmetic

The eight conversion rows are in `tests/board/rtos/app/cases/rtc.c`; the same
arithmetic can be checked against Python's `datetime` on any host:

```python
from datetime import datetime, timezone
for s in ("1970-01-01 00:00:00", "2000-02-29 12:34:56",
          "2026-01-02 03:04:05", "2038-01-19 03:14:08",
          "2099-12-31 23:59:59"):
    d = datetime.strptime(s, "%Y-%m-%d %H:%M:%S").replace(tzinfo=timezone.utc)
    print(s, int(d.timestamp()), d.isoweekday())
```
