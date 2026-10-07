# patches/cherrysh - index

Local patches replayed onto the vendored CherrySH component (`components/cherrysh/`).
Empty at import time: the mapping is zero patch and the snapshot is byte-identical
to the pin.

| Item | Value |
|---|---|
| Upstream | https://github.com/cherry-embedded/CherrySH.git |
| Base (= pin) | `8efe539c6e55b71d2f2cd1116cd00c4d64222a67` (`v1.0.1-20-g8efe539`) |
| Patch list | none |
| Submission status | n/a |

Rules (project patch policy):

* A needed library-body fix is written here as an upstreamable patch
  (`git format-patch` style: one concern per patch, English, `Signed-off-by`, no
  local knowledge) and submitted upstream; the vendored snapshot itself stays
  byte-identical, so a patch is applied only through the project's patch channel and
  is recorded in `components/cherrysh/DIVERGENCES.md` while it is in force.
* When upstream accepts a patch: remove it here, move the pin forward, and note
  both in `components/cherrysh/PROVENANCE.md` and `DIVERGENCES.md`.
