# Provenance: the tracked wireless firmware images

The image has no filesystem: a driver's `request_firmware()` is answered by
`fw_registry.c` from a table the image fills at start-up, and the table's data
is a C unit generated from the vendor image beside this file.  Until
2026-11-11 that vendor image lived only in the GT build tree
(`linux-upstream/build/gt-20261110/...`), so a clean clone of this repository
could not build a wireless image.  The images are now tracked inputs here; the
generated C units stay build products.

## The images

| | AC 7260 | RTL8851BU |
|---|---|---|
| File | `iwlwifi-7260-17.ucode` | `rtw89/rtw8851b_fw-1.bin` |
| Request name | `iwlwifi-7260-17.ucode` | `rtw89/rtw8851b_fw-1.bin` |
| Size | 1,049,340 bytes | 1,408,706 bytes |
| SHA-256 | `5d81a6003df0228a497ad27f916ba2c979614b4c439b0f45a5f2873dc0607fe8` | `1c4ccb5fa06bf5649146025e95993db66876a1fbc0bd766ec05686587f69c1b7` |
| Source | the GT line's initramfs, `lib/firmware/iwlwifi-7260-17.ucode`, carried unchanged from the pin `63a2a8ab9cd3` work tree | the GT line's initramfs, `lib/firmware/rtw89/rtw8851b_fw-1.bin`, and the previous line's tracked `firmware/rtw89/` copy; both are this digest |
| Modification | none (byte-identical to the image the GT line booted with) | none (byte-identical to the image the GT line booted with) |
| License | Intel binary firmware licence, `LICENCE.iwlwifi_firmware` beside this file | Realtek binary redistribution terms, `LICENCE.realtek_firmware` beside this file |
| Generated unit | `iwl7260_fw_blob.c`, `lkpi_fw_iwl7260_fw_register()` | `rtw8851b_fw_blob.c`, `lkpi_fw_rtw8851b_fw_register()` |

The 8851BU asks for the `-1` format first (`fw.c` walks the format number
downwards from the chip's maximum), so that is the name the registry is
expected to carry.  Only the `-1` blob is tracked: it is the one the GT line
loaded (`loaded firmware rtw89/rtw8851b_fw-1.bin`, `Firmware version
0.29.41.5`), and registering a name no probe asks for would hide a fallback.

## Verification

```sh
sha256sum components/liblinux80211/fw/iwlwifi-7260-17.ucode
# 5d81a6003df0228a497ad27f916ba2c979614b4c439b0f45a5f2873dc0607fe8
sha256sum components/liblinux80211/fw/rtw89/rtw8851b_fw-1.bin
# 1c4ccb5fa06bf5649146025e95993db66876a1fbc0bd766ec05686587f69c1b7
tools/rtos/fw-blobs.py --check
```

`tools/rtos/fw-blobs.py` regenerates the units from these files and refuses a
source whose SHA-256 is not the pinned value; the build runs the generator,
and `--check` verifies both the source hashes and the emitted text.

The licence texts are tracked copies rather than references: each licence's
redistribution condition is that the notice travels with the binary, and a
clean clone has no other copy of it.
