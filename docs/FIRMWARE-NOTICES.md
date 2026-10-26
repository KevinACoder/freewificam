# Firmware notices - the blobs the RTOS images compile in

The RTOS bin platform has no filesystem, so a driver's `request_firmware()` is
served by `components/linuxkpi/rtos/fw_registry_rtos.c` from a table filled at
start-up. The table's data is a C array generated from the firmware file the
Linux lane ships for the same chip.

Two rules keep this honest:

1. **The firmware is not in git.** The bytes are the vendor's binary, not
   source: they are excluded by `.gitignore` (`components/linuxkpi/fw/`) and
   redistributed only under the vendor's terms recorded below. What is in git is
   the generator (`tools/rtos/fw-blob.py`) and this notice.
2. **A blob is trusted by its digest, not by its presence.** The generator
   writes the source sha256 and size into the blob's header and asserts the
   array's size at compile time; `fw-blob.py --check` re-extracts the array and
   compares it with the source file.

## The blobs

| chip | request name | bytes | sha256 | source of the bytes | licence |
|---|---|---|---|---|---|
| RTL8188EUS | `rtlwifi/rtl8188eufw.bin` | 15,262 | `2ff74315287529dec2e50eb57d6e0c97d2116f28ae166773ccdf93b6360000c4` | `linux-upstream/build/e4ap5g1-itx/initramfs/rootfs/lib/firmware/rtlwifi/rtl8188eufw.bin` (the GT image's file; also in the mfs rootfs via `build-mfs-rootfs.sh`'s `FIRMWARE_FILES`) | Realtek binary redistribution, below |
| RTL8851BU | `rtw89/rtw8851b_fw-1.bin` | 1,408,706 | `1c4ccb5fa06bf5649146025e95993db66876a1fbc0bd766ec05686587f69c1b7` | the GT image's `rtw89/rtw8851b_fw-1.bin` | Realtek binary redistribution |

(An image may also carry the AC 7260 blob; its own header records the digest,
the same way.)

## Generating the 8188EU blob

```sh
tools/rtos/fw-blob.py \
    --in ../linux-upstream/build/e4ap5g1-itx/initramfs/rootfs/lib/firmware/rtlwifi/rtl8188eufw.bin \
    --name rtlwifi/rtl8188eufw.bin --symbol rtl8188eufw \
    --sha256 2ff74315287529dec2e50eb57d6e0c97d2116f28ae166773ccdf93b6360000c4 \
    --size 15262 \
    --out components/linuxkpi/fw/rtl8188eufw_blob.c

# verify a generated blob against the source file
tools/rtos/fw-blob.py --check components/linuxkpi/fw/rtl8188eufw_blob.c \
    --in ../linux-upstream/build/e4ap5g1-itx/initramfs/rootfs/lib/firmware/rtlwifi/rtl8188eufw.bin \
    --name rtlwifi/rtl8188eufw.bin
```

The generated unit registers itself as `lkpi_fw_rtl8188eufw_register()`; the
image's linker line carries `-Wl,-u,lkpi_fw_rtl8188eufw_register` so
`--gc-sections` cannot drop it, and `make gates` checks the symbol is present.

## Realtek binary redistribution terms (RTL8188EU firmware)

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

The RTL8851BU blob carries the same Realtek terms; its notice travels with its
blob (`components/linuxkpi/fw/rtw8851b_fw.LICENCE.txt`, not in git for the same
reason as the bytes).

## Why the blobs are not committed

Same ruling as `decisions/2026-10-25-pcie-rtos-ladder.md`: the repository imports
source, and source-only trees are reproducible; a binary blob's home is the
vendor's redistribution terms plus the pinned Linux firmware file, not the
repository. The price is one build input outside git, and the generator plus the
digest check is how that price is kept visible rather than becoming a mystery
missing file.