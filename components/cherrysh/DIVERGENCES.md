# components/cherrysh - divergences

Deviations of this component from its upstream pin. The bytes of every kept file are
identical to the pin; nothing has been changed and no patch is in force.

| # | File(s) | Change | Reason | Round |
|---|---|---|---|---|
| - | (none) | no divergence at import | imported verbatim | M0-15 |

Import state (2026-10-07): all 14 kept files are byte-identical to
`8efe539c6e55b71d2f2cd1116cd00c4d64222a67` (`sha256sum -c MANIFEST.sha256`, 14/14);
no patch is applied.

Rules for future changes:

* Prefer an upstreamable patch under `patches/cherrysh/` over an in-tree edit.
* If an in-tree edit is unavoidable, keep it minimal, mark it with
  `/* CHERRYSH_IMPORT(L): <what and why> */` and add a row here with the consuming round.
* Port-side deviations (the config header, the shell port, the console backends) are
  the port's own code and are not recorded here.
