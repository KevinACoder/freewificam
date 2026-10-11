# Design documentation

This directory holds the project's design documentation: how the stack is put
together, and what its boundaries are. It is written for a reader who wants to
understand or port the stack, not just build it.

| Document | Covers |
|---|---|
| [net80211.md](net80211.md) | The wireless core: the vendored net80211 and the thin port layer under it |
| [bus-and-threading.md](bus-and-threading.md) | The bus interface layers (USB / PCIe / SDIO) and the threading model |
| [memory-model.md](memory-model.md) | The heap, the packet pools and the ownership rules |
| [known-limitations.md](known-limitations.md) | What is not done, and what is known to be weak |
| [RELEASE-NOTES-0.1.md](RELEASE-NOTES-0.1.md) | The 0.1 release notes: positioning, hardware, status, license |

## Provenance

Everything that is not first-party code is vendored, pinned and recorded. The
authority is [../LICENSES.md](../LICENSES.md): it lists every vendored tree,
imported unit and binary blob with its license and the obligations that follow.

The shape of an imported unit:

- the bytes are a verbatim snapshot of a pinned upstream revision;
- a `PROVENANCE.md` next to it records the upstream, the pin, the license, the
  compiled set and the reason it is in the tree;
- a `DIVERGENCES.md` records every local difference, one entry per change, with
  the reason and the round that made it;
- a `MANIFEST.sha256` anchors the vendored bytes, so a stray edit is visible.

Upstream fixes and features are not made in the tree. They are written as
patches under [../patches/](../patches/) in community style (one concern per
patch, English, `Signed-off-by`) and replayed at build time; each patch series
records its base commit and submission status in `patches/<repo>/README.md`.

The design pattern behind each new module is named in the source with an
`@design` line pointing at a mature precedent (`file:line`), so the structure
can be checked against something that already exists rather than invented.
