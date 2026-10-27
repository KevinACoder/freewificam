# PATCH-POLICY - imported source, ports and patches together

Every imported tree is carried as **source + ports + patches** in its own
directory:

```
third-party/<component>/          imported source (already patched)
third-party/<component>/ports/    the first-party port for that component
third-party/<component>/patches/  the patch record + digest of the source
wireless-drivers/<driver>/        same three-part shape
components/net80211/              first-party imported core: source + ports + patches
```

Patches are **applied in the tree by default**. The checked-out source is the
source the build compiles; there is no build-time replay, no patched copy and no
top-level `patches/` directory. `patches/` is the record: it keeps the
upstreamable patch file, a README naming the pin and the submission state, and
`MANIFEST.sha256`, the digest of the source tree (excluding `ports/` and
`patches/`).

Verify an imported tree from its own directory:

```sh
cd third-party/<component> && sha256sum -c patches/MANIFEST.sha256
cd wireless-drivers/<driver> && sha256sum -c patches/MANIFEST.sha256
cd components/net80211 && sha256sum -c patches/MANIFEST.sha256
```

The RTOS build's `digest` target checks every `third-party/*/patches` and the
wireless drivers that carry a manifest.

| tree | pin | patches |
|---|---|---|
| `third-party/CMSIS_6` | `v6.3.0` `45dab712` | none |
| `third-party/CMSIS-FreeRTOS` | `v11.3.0` `4dd00351` | `0001` (64-bit handle round trip), applied |
| `third-party/FreeRTOS-Kernel` | `V11.3.1` `3a22924e` | none |
| `third-party/lwip` | `STABLE-2_2_1_RELEASE` `77dcd25a` | `0003` (delayed-ACK watermark), applied; `0001`/`0004`/`0006` parked under `patches/unadopted/` |
| `third-party/threadx` | `44d7c95c` (`6.5.1.202602a`) | none |
| `third-party/fsl_sdmmc` | NXP SDMMC stack | none |
| `third-party/printf`, `tlsf`, `ulog` | per-component pin | none |
| `wireless-drivers/iwlwifi` | `e71d9a28760f` | none |
| `wireless-drivers/rtl8xxxu` | `e63969708249` | none |
| `wireless-drivers/rtw88` | `e71d9a28760f` | none |
| `wireless-drivers/rtw89` | `79e12cc9` | none |
| `wireless-drivers/rtl8189fs` | `13bbdbc` | none; the vendor bytes are vendored unmodified |

Patch form and lifecycle: one concern per patch, `git format-patch` form, English,
`Signed-off-by`, no local knowledge; written so it could be sent upstream as-is.
When upstream accepts a patch, remove it from `patches/`, move the pin forward,
regenerate the manifest, and record both in the component's provenance file.

The `wireless-drivers/rtl8189fs` tree is GPL-2.0 and is vendored unmodified at
its pin (`src/`), with its licence in `LICENSE` and its record in
`PROVENANCE.md`. Its `patches/` records a vendor fix when one is adopted; the
build then applies it to a build-local copy, so the vendored bytes stay at the
pin. `VENDOR_8189FS` names an alternative checkout for a deliberate comparison
build, not the normal path.
