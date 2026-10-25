# iwlwifi/patches - record only

The iwlwifi tree is imported **already patched**. This directory records the
patch set and the digest of the source tree the build compiles; it is not a
build-time replay directory.

| Item | Value |
|---|---|
| Upstream | mainline Linux `drivers/net/wireless/intel/iwlwifi/` |
| Pin | `e71d9a28760f605082308c2e1114627d406b4a29` (7.3.0-rc2) |
| Patch list | none (zero patch) |
| Manifest | `MANIFEST.sha256` - the digest of the imported tree, excluding this directory |
| Verify | `cd wireless-drivers/iwlwifi && sha256sum -c patches/MANIFEST.sha256` |

Why zero patches: the driver is consumed as mainline wrote it. Everything the
port needs that the driver does not carry — the PCI/DMA/interrupt surface, the
mac80211/cfg80211 header face, the net80211 bridge, the module entry and the PCI
registration glue — lives in first-party translation units. The units the 7260
path does not reach are excluded by the build's translation-unit list, not by
editing source.

If a real defect has to be fixed, the patch is applied to the tree in this
repository and the patch file is kept here as the record; the build compiles the
tree directly, with no replay step. Keep one concern per patch, English,
`git format-patch` form, `Signed-off-by`.
