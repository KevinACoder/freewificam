# rtw88/patches - record only

The rtw88 tree is imported **already patched**. This directory records the patch
set and the digest of the source tree the build compiles; it is not a build-time
replay directory.

| Item | Value |
|---|---|
| Upstream | mainline Linux `drivers/net/wireless/realtek/rtw88/` |
| Pin | `e71d9a28760f605082308c2e1114627d406b4a29` (7.3.0-rc2) |
| Patch list | none (zero patch) |
| Manifest | `MANIFEST.sha256` - the digest of the imported tree, excluding this directory |
| Verify | `cd wireless-drivers/rtw88 && sha256sum -c patches/MANIFEST.sha256` |

If a real defect has to be fixed, the patch is applied to the tree in this
repository and the patch file is kept here as the record; the build compiles the
tree directly, with no replay step. Keep one concern per patch, English,
`git format-patch` form, `Signed-off-by`.
