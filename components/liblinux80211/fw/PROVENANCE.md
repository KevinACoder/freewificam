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

## The later cards (2026-11-14)

| | RTL8821CU (rtw88) | RTL8188EU (rtl8xxxu) |
|---|---|---|
| File | `rtw88/rtw8821c_fw.bin` | `rtlwifi/rtl8188eufw.bin` |
| Request name | `rtw88/rtw8821c_fw.bin` | `rtlwifi/rtl8188eufw.bin` |
| Size | 139,472 bytes | 15,262 bytes |
| SHA-256 | `2ef409bc418549fcf294061dd0cae1fc22fd9da79b60524950b25de18732f3f0` | `2ff74315287529dec2e50eb57d6e0c97d2116f28ae166773ccdf93b6360000c4` |
| Source | the previous line's tracked `firmware/rtw88/` copy (same digest), which is the image its GT line booted with | the previous line's tracked `firmware/rtlwifi/` copy (same digest); the GT initramfs's `lib/firmware/rtlwifi/rtl8188eufw.bin` is the same image, and the previous line's board run printed `Firmware revision 28.0 (signature 0x88e1)` from it |
| Modification | none | none |
| License | Realtek binary redistribution terms, `LICENCE.realtek_firmware` beside this file | Realtek binary redistribution terms, `LICENCE.realtek_firmware` beside this file |
| Generated unit | `rtw8821c_fw_blob.c`, `lkpi_fw_rtw8821c_fw_register()` | `rtl8188eufw_blob.c`, `lkpi_fw_rtl8188eufw_register()` |

## The RTL8723BU (2026-11-15)

| | RTL8723BU (rtl8xxxu) |
|---|---|
| File | `rtlwifi/rtl8723bu_nic.bin` |
| Request name | `rtlwifi/rtl8723bu_nic.bin` |
| Size | 32,108 bytes |
| SHA-256 | `a52b5a7be4841b4b2839eddf5122b3300a3610866abe4bb3c3c9e444c8ab7969` |
| Source | the GT line's initramfs (`linux-upstream/build/e4ap5g1-itx/initramfs/rootfs/lib/firmware/rtlwifi/rtl8723bu_nic.bin`); the same blob the 2026-11-14 gt1 boot loaded for the board's EHCI 8723BU, which printed `Firmware revision 35.0 (signature 0x5301)` |
| Modification | none |
| License | Realtek binary redistribution terms, `LICENCE.realtek_firmware` beside this file |
| Generated unit | `rtl8723bu_nic_blob.c`, `lkpi_fw_rtl8723bu_nic_register()` |

Only the chip's profile links it (`CONFIG_RTL8XXXU_CHIP_8723BU`): the 8188EU
image keeps its own blob, and an image never carries a firmware name the
driver's probe on that bench will not ask for.

Both names are the literal `MODULE_FIRMWARE()` strings of their drivers, which
is the name `request_firmware()` is called with; the registry matches by name,
so a different spelling would be a silent "no firmware".

## Verification

```sh
sha256sum components/liblinux80211/fw/iwlwifi-7260-17.ucode
# 5d81a6003df0228a497ad27f916ba2c979614b4c439b0f45a5f2873dc0607fe8
sha256sum components/liblinux80211/fw/rtw89/rtw8851b_fw-1.bin
# 1c4ccb5fa06bf5649146025e95993db66876a1fbc0bd766ec05686587f69c1b7
sha256sum components/liblinux80211/fw/rtw88/rtw8821c_fw.bin
# 2ef409bc418549fcf294061dd0cae1fc22fd9da79b60524950b25de18732f3f0
sha256sum components/liblinux80211/fw/rtlwifi/rtl8188eufw.bin
# 2ff74315287529dec2e50eb57d6e0c97d2116f28ae166773ccdf93b6360000c4
sha256sum components/liblinux80211/fw/rtlwifi/rtl8723bu_nic.bin
# a52b5a7be4841b4b2839eddf5122b3300a3610866abe4bb3c3c9e444c8ab7969
tools/rtos/fw-blobs.py --check
```

`tools/rtos/fw-blobs.py` regenerates the units from these files and refuses a
source whose SHA-256 is not the pinned value; the build runs the generator,
and `--check` verifies both the source hashes and the emitted text.

The licence texts are tracked copies rather than references: each licence's
redistribution condition is that the notice travels with the binary, and a
clean clone has no other copy of it.
