# Provenance: the tracked AC 7260 firmware image

The image has no filesystem: iwlwifi's `request_firmware()` is answered by
`fw_registry.c` from a table the image fills at start-up, and the table's data
is a C unit generated from the vendor image.  Until 2026-11-11 that vendor
image lived only in the GT build tree (`linux-upstream/build/gt-20261110/...`),
so a clean clone of this repository could not build a wireless image.  The
image is now a tracked input here; the generated C unit stays a build product.

| | |
|---|---|
| File | `iwlwifi-7260-17.ucode` |
| Request name | `iwlwifi-7260-17.ucode` |
| Size | 1,049,340 bytes |
| SHA-256 | `5d81a6003df0228a497ad27f916ba2c979614b4c439b0f45a5f2873dc0607fe8` |
| Source | the GT line's initramfs, `lib/firmware/iwlwifi-7260-17.ucode`, carried unchanged from the pin `63a2a8ab9cd3` work tree |
| Modification | none (byte-identical to the image the GT line booted with) |
| License | Intel binary firmware licence, `LICENCE.iwlwifi_firmware` beside this file; binary redistribution is permitted for unmodified images, with the copyright notice reproduced |

The generated unit `iwl7260_fw_blob.c` is not tracked.  `tools/rtos/fw-blobs.py`
regenerates it from this file and refuses a source whose SHA-256 is not the
pinned value; the build runs the generator, and `--check` verifies both the
source hash and the emitted text.

## Verification

```sh
sha256sum components/liblinux80211/fw/iwlwifi-7260-17.ucode
# 5d81a6003df0228a497ad27f916ba2c979614b4c439b0f45a5f2873dc0607fe8
tools/rtos/fw-blobs.py --check
```

The Intel licence text is a tracked copy rather than a reference: the
licence's redistribution condition is that the notice travels with the binary,
and a clean clone has no other copy of it.
