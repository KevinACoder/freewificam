# patches/tlsf - local patches replayed onto the vendored package

| Upstream | tlsf.baisoku.org, TLSF v3.1 package (2016-04-10); no canonical git upstream |
| Base (= pin) | the v3.1 package as vendored in `components/tlsf/` (contract: `components/tlsf/MANIFEST.sha256`, 2/2) |
| Patch list | none - the vendored bytes are byte-identical to the package and no patch is in force |
| Submission status | not applicable (nothing to submit); a future fix would be written in `git format-patch` form and listed here |

Rules (project patch policy): the vendored snapshot stays byte-identical; a patch is
applied only through `tools/patches/replay.sh`, one concern per patch, English,
`Signed-off-by`, no local knowledge.
