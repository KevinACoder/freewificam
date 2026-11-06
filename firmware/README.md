# firmware - the vendor images this repository's images compile in

An RTOS image has no filesystem, so every driver firmware is a C array linked
into the image.  The arrays are generated from the files in this directory, and
this directory is the firmware's home in the repository: a checkout that has
this tree can build every image without a second source, a network fetch or a
machine-local `/lib/firmware`.

The bytes are the vendor's binary, unmodified, redistributed under the terms
next to them:

| path | request name / user | licence |
|---|---|---|
| `rtlwifi/rtl8188eufw.bin` | `rtlwifi/rtl8188eufw.bin` (rtl8xxxu / 8188EU) | Realtek, `REALTEK-LICENCE.txt` |
| `rtlwifi/rtl8723bu_nic.bin` | `rtlwifi/rtl8723bu_nic.bin` (rtl8xxxu / 8723BU) | Realtek, `REALTEK-LICENCE.txt` |
| `rtw88/rtw8821c_fw.bin` | `rtw88/rtw8821c_fw.bin` (rtw88 / 8821CU, 8821CS) | Realtek, `REALTEK-LICENCE.txt` |
| `rtw89/rtw8851b_fw-1.bin` | `rtw89/rtw8851b_fw-1.bin` (rtw89 / 8851BU) | Realtek, `REALTEK-LICENCE.txt` |
| `iwlwifi/iwlwifi-7260-17.ucode` | `iwlwifi-7260-17.ucode` (iwlwifi / AC 7260) | Intel, `INTEL-LICENCE.txt` |
| `aic8800/*.bin` (five) | the AIC8800D80 U02 set, downloaded by the vendor stack | GPL-3.0, `GPL-3.0.txt` |

`MANIFEST.sha256` pins the exact bytes (`sha256sum -c MANIFEST.sha256` in this
directory).  `docs/FIRMWARE-NOTICES.md` carries the same digests with the
upstream each file came from and the redistribution terms in full.

## Where the bytes came from

The Realtek and Intel files are the ones the Linux kernel loads for the same
chips: the Linux firmware package's `rtlwifi/`, `rtw88/`, `rtw89/` and
`iwlwifi-7260-17.ucode` files, taken from the reference board image's
`/lib/firmware`.  The five AIC8800D80 U02 images are the vendor set published
in the Radxa `aic8800` tree (`src/USB/driver_fw/fw/aic8800D80`, commit
`df4c783b`), which is the same set the CherryUSB demo links in.

## The generated units

`tools/rtos/fw-blobs.py` is the one table (source file, request name, C symbol,
output unit) and the generator:

```sh
tools/rtos/fw-blobs.py --all      # (re)write every blob that is out of date
tools/rtos/fw-blobs.py --check --all
```

The outputs are `components/linuxkpi/fw/<stem>_blob.c` (the linuxkpi registry
carrier) and `wireless-drivers/aic8800/fw/aic8800_fw_blobs.c` (the vendor
stack's own carrier).  Both are build products, gitignored, and regenerated
automatically by the build when missing or stale - their content is a function
of this directory, so nothing else needs to be remembered.

`tools/rtos/fw-blob.py` remains as the single-file form (one source, one
output): it is what an external consumer uses to make a blob for a chip this
repository does not carry.
