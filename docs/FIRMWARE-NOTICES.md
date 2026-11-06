# Firmware notices - the blobs the RTOS images compile in

The RTOS bin platform has no filesystem, so a driver's `request_firmware()` is
served by `components/linuxkpi/rtos/fw_registry_rtos.c` from a table filled at
start-up (and the AIC8800 vendor stack is handed its five images the same way).
The table's data is a C array generated from the vendor's firmware file for the
same chip.

The vendor files live in this repository under `firmware/` - the subtrees keep
the names the drivers ask for (`rtlwifi/`, `rtw88/`, `rtw89/`, `iwlwifi/`) plus
`aic8800/` for the vendor set - with `firmware/MANIFEST.sha256` pinning the
bytes and the redistribution terms next to them.  A checkout therefore builds
every image without a second source, a network fetch or a machine-local
`/lib/firmware`.

Two rules keep this honest:

1. **The generated arrays are not in git.** They are a build product of
   `firmware/`: `components/linuxkpi/fw/*_blob.c` and
   `wireless-drivers/aic8800/fw/aic8800_fw_blobs.c` are gitignored, and
   `tools/rtos/fw-blobs.py` (the generator) plus `firmware/` (the bytes) are
   what is committed.  The build runs the generator itself
   (`tests/board/rtos/build/fw.mk`), so there is no manual step to forget.
2. **A blob is trusted by its digest, not by its presence.** The generator
   writes the source sha256 and size into the blob's header and asserts the
   array's size at compile time; `fw-blobs.py --check` re-extracts every array
   and compares it with the file in `firmware/`.

## The images

The Realtek and Intel files are the ones the Linux kernel loads for the same
chip, published by the Linux firmware package (`linux-firmware`); the AIC8800
set is the vendor's GPL-3.0 USB firmware.

| chip | request name | bytes | sha256 | file | licence |
|---|---|---|---|---|---|
| RTL8188EUS | `rtlwifi/rtl8188eufw.bin` | 15,262 | `2ff74315287529dec2e50eb57d6e0c97d2116f28ae166773ccdf93b6360000c4` | `firmware/rtlwifi/rtl8188eufw.bin` | Realtek binary redistribution, below |
| RTL8723BU | `rtlwifi/rtl8723bu_nic.bin` | 32,108 | `a52b5a7be4841b4b2839eddf5122b3300a3610866abe4bb3c3c9e444c8ab7969` | `firmware/rtlwifi/rtl8723bu_nic.bin` | Realtek binary redistribution, below |
| RTL8821CS / RTL8821CU | `rtw88/rtw8821c_fw.bin` | 139,472 | `2ef409bc418549fcf294061dd0cae1fc22fd9da79b60524950b25de18732f3f0` | `firmware/rtw88/rtw8821c_fw.bin` | Realtek binary redistribution, below |
| RTL8851BU | `rtw89/rtw8851b_fw-1.bin` | 1,408,706 | `1c4ccb5fa06bf5649146025e95993db66876a1fbc0bd766ec05686587f69c1b7` | `firmware/rtw89/rtw8851b_fw-1.bin` | Realtek binary redistribution, below |
| Intel AC 7260 | `iwlwifi-7260-17.ucode` | 1,049,340 | `5d81a6003df0228a497ad27f916ba2c979614b4c439b0f45a5f2873dc0607fe8` | `firmware/iwlwifi/iwlwifi-7260-17.ucode` | Intel, `firmware/INTEL-LICENCE.txt` |
| AIC8800D80 | `fw_patch_table_8800d80_u02.bin` | 1,384 | `9decb77435b7e9713e33e32da483d683b7329ed93b672b2d1b134031d7da5f67` | `firmware/aic8800/` | GPL-3.0, `firmware/GPL-3.0.txt` |
| AIC8800D80 | `fw_adid_8800d80_u02.bin` | 1,708 | `a526cbd02fcdc495f049f3ad6b5933cb08cd984b16790c716a060d582fee1a56` | `firmware/aic8800/` | GPL-3.0 |
| AIC8800D80 | `fw_patch_8800d80_u02.bin` | 32,700 | `f0e2f5bbc17bc327ca7f1574ff55370dfd863d931514347bb4abc18a74f6218f` | `firmware/aic8800/` | GPL-3.0 |
| AIC8800D80 | `fw_patch_8800d80_u02_ext0.bin` | 16,136 | `ca738f5ea5aca6021c4f48b60ff35e835dfdaf072a8490e3ba8ba0a42440e3e5` | `firmware/aic8800/` | GPL-3.0 |
| AIC8800D80 | `fmacfw_8800d80_u02.bin` | 358,072 | `23f2bed2ac0e8f7bda8f36df7defd30d9b928c97867f6f0f01f28d89dd78d670` | `firmware/aic8800/` | GPL-3.0 |

The AIC8800D80 set is the U02 USB set for revision `0x07` non-H devices (BootROM
chip register `0xf3078820` on the validated stick); it comes from the Radxa
`aic8800` tree (`src/USB/driver_fw/fw/aic8800D80`, commit `df4c783b`), the same
set the CherryUSB demo links in.  `firmware/aic8800/README.md` keeps the load
addresses.

## Generating the blobs

One table, one command.  `tools/rtos/fw-blobs.py` knows every stem, the file it
comes from, the request name the driver asks for and the unit it writes:

```sh
# (re)write every blob that is missing or out of date - the build does this
tools/rtos/fw-blobs.py --all

# one stem: rtl8188eufw rtl8723bu rtw8821c_fw rtw8851b_fw iwl7260_fw aic8800
tools/rtos/fw-blobs.py --stem rtw8851b_fw

# verify the generated units against firmware/ (and their headers)
tools/rtos/fw-blobs.py --check --all
```

The linuxkpi blobs land in `components/linuxkpi/fw/<stem>_blob.c` and register
themselves as `lkpi_fw_<stem>_register()`; the image's linker line carries
`-Wl,-u,lkpi_fw_<stem>_register` so `--gc-sections` cannot drop them, and the
image gate checks the symbol is present.  The AIC8800 unit lands in
`wireless-drivers/aic8800/fw/aic8800_fw_blobs.c` and registers as
`aic8800_fw_blobs_register()`, forced the same way.

The stem is not decoration: the 8821CS line's stem carries `_fw`
(`lkpi_fw_rtw8821c_fw_register`) because that is the name `rtw88_sdio.mk` and
the case call, and the 8851B registers the `-1` form because rtw89 walks its
format numbers downwards from the chip's maximum (`fw.c:1006-1010`,
`fw.h:4654-4661`).  A blob generated under another name registers a symbol
nothing references and the image fails to link.

### Adding a chip this repository does not carry

`tools/rtos/fw-blob.py` is the single-file form - one source, one output - and
is what an external consumer uses for a chip outside `firmware/`:

```sh
tools/rtos/fw-blob.py \
    --in "$FIRMWARE_DIR"/rtlwifi/rtl8188eufw.bin \
    --name rtlwifi/rtl8188eufw.bin --symbol rtl8188eufw \
    --sha256 2ff74315287529dec2e50eb57d6e0c97d2116f28ae166773ccdf93b6360000c4 \
    --size 15262 \
    --out components/linuxkpi/fw/rtl8188eufw_blob.c
```

A chip that an image in this repository links belongs in `firmware/` and in the
table of `fw-blobs.py` instead, or its image is a build that depends on a file
outside the tree.

## Realtek binary redistribution terms

```
Copyright (c) 2010, Realtek Semiconductor Corporation
All rights reserved.

Redistribution.  Redistribution and use in binary form, without
modification, are permitted provided that the following conditions are
met:

* Redistributions must reproduce the above copyright notice and the
  following disclaimer in the documentation and/or other materials
  provided with the distribution.
* Neither the name of Realtek Semiconductor Corporation nor the names of its
  suppliers may be used to endorse or promote products derived from this
  software without specific prior written permission.
* No reverse engineering, decompilation, or disassembly of this software
  is permitted.

Limited patent license. Realtek Semiconductor Corporation grants a world-wide,
royalty-free, non-exclusive license under patents it now or hereafter
owns or controls to make, have made, use, import, offer to sell and
sell ("Utilize") this software, but solely to the extent that any
such patent is necessary to Utilize the software alone, or in
combination with an operating system licensed under an approved Open
Source license as listed by the Open Source Initiative at
http://opensource.org/licenses.  The patent license shall not apply to
any other combinations which include this software.  No hardware per
se is licensed hereunder.

DISCLAIMER.  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
DAMAGE.
```

The same terms are in `firmware/REALTEK-LICENCE.txt`, which travels with the
bytes.  The Intel notice is `firmware/INTEL-LICENCE.txt`; the AIC8800 images
are GPL-3.0 (`firmware/GPL-3.0.txt`), published under that licence by the
vendor's own tree.

## Why the arrays are not committed

The repository imports source, and source-only trees are reproducible; a
generated C array of a vendor binary is a build product of `firmware/`, and
having both would be two copies of the same bytes with two ways to drift.  The
price is one generator invocation per image (a no-op when the arrays are
current), and `fw-blobs.py --check` plus `firmware/MANIFEST.sha256` is how that
price is kept visible.
