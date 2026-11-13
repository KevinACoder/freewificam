# 8189-sdio-trace — the GT arm's SDIO command geometry instrument

This is an **observation-only** patch against the vendored RTL8189FS driver at
the GT pin (`linux-upstream` @ `b98a1dfd4661`, Image `a36beece…`).  It is applied
to a build copy, never to the pinned tree, and the module it produces is the
`instrumented` arm only: the GT denominator still comes from the clean ko
(`9e3f6729…`).

## Why

The RTOS freeze is a transfer that never returned.  The RTOS side can say which
kind/address/length that transfer was (the engine's trace), but GT has no
equivalent per-op view: the GT kernel has no `CONFIG_FTRACE`/`CONFIG_KPROBES`,
so `mmc:*` tracepoints and kprobes are unavailable.  This patch puts the same
kind/address/bytes/duration/return record at the vendor's own entry points, so
the two sides can be compared at the level both share: the commands the driver
issued.

Covered entry points (the vendor's own funnels):

- `_sd_cmd52_read` / `_sd_cmd52_write` — the local register path
- `_sd_read8` / `sd_read8` / `sd_read16` / `_sd_read32` / `sd_read32`
- `sd_write8` / `sd_write16` / `_sd_write32` / `sd_write32`
- `_sd_read` / `_sd_write` — the CMD53 data path

Vendor code that calls the kernel's `sdio_*` helpers directly (a few retry and
raw paths) is not covered, so CMD52 counts are a lower bound; the data path is
complete.

## Apply and build

```sh
cp -a linux-upstream/drivers/net/wireless/realtek/rtl8189fs freewificam/build/gt-8189-trace/rtl8189fs
patch -p1 -d freewificam/build/gt-8189-trace/rtl8189fs \
    < freewificam/tools/gt/patches/8189-sdio-trace/sdio_ops_linux.c.patch
make -C linux-upstream O="$PWD/linux-upstream/build/gt-20261113-8189" \
    ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
    M="$PWD/freewificam/build/gt-8189-trace/rtl8189fs" modules -j8
```

The patch is against the file as shipped in the pin; `patch -p0` from the
driver root applies it (the diff was taken with paths `.../rtl8189fs/os_dep/...`
stripped).

## Read it

The module parameter is a one-shot dump trigger; nothing prints during the
measured window:

```sh
echo 1 > /sys/module/rtl8189fs/parameters/sdio_trace_dump
dmesg | sed -n '/RTWTR begin/,/RTWTR end/p'
```

Fields:

```
RTWTR GEOM k=c52r n=… bytes=… avg_us=… max_us=…     (one line per kind)
RTWTR OP t=<us> dur=<us> k=<kind> addr=<5 hex> bytes=<n> blk=<n> ret=<s32>
```

`kind`: `c52r`, `c52w`, `ext_r`, `ext_w`.  `t` is microseconds since boot;
`dur` is the vendor call's own duration.  The ring keeps the last 64 ops.

`sdio_trace=0` disables the accounting at run time (default 1).

## Fingerprint

- ko sha256 `a710106b39a423c907b6cfbc3d7d80299b2b0fafc2b5424fb0f8793d6949af9b`
  — the G1 arm's build.  Its aggregate counters are valid; its per-op ring dump
  is empty because the reader's index omitted the ring wrap (`at = head + RING -
  n + i` instead of `head - n + i`), so every entry failed the sequence check.
- ko sha256 `ad9e2a48f9cf6061e0c723ad96cf8adf885c6d33e0aee220a6b0f54f2d7dbdb4`
  — same source with the ring index fixed; use this one for the next geometry
  arm.
- vermagic `7.3.0-rc2-gb98a1dfd4661 SMP preempt mod_unload aarch64` (matches the
  GT Image; the Image itself is unchanged).

## License

The patched file is vendor code under GPL-2.0-only; this patch is distributed
under the same license, and the instrumented module is a GPL-2.0 link product
(`docs/LICENSES.md`).
