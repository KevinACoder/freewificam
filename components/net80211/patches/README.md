# net80211/patches - record only

The net80211 component is imported source, the project's wireless core and
therefore first-party. It follows the same three-part shape as the other imported
components: **source + ports + patches**.

| Item | Value |
|---|---|
| Upstream | https://github.com/KevinACoder/net_80211.git (its `net80211/` tree is itself a verbatim NetBSD import) |
| Pin | `9770b1ad7093f43a0f2ab2dc8d980067fb0f62a1` (tag `netbsd-shape-freeze`) |
| Patch list | none (zero patch): the 61 kept files are byte-identical to the pin |
| Manifest | `MANIFEST.sha256` - the digest of the kept source tree, excluding this directory and the first-party `ports/` |
| Verify | `cd components/net80211 && sha256sum -c patches/MANIFEST.sha256` |
| Record | the trim and the port-side machinery are recorded in `DIVERGENCES.md`; the port itself is `ports/` |

If an upstreamable change is needed, the patch is applied to the tree and the
patch file is kept here as the record; the build compiles the tree directly. If
an in-tree edit is unavoidable, keep it minimal, mark it with
`/* NET80211_PORT(L): <what and why> */` and add a row to `DIVERGENCES.md`.
