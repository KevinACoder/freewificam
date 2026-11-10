# LICENSES

| Part | License |
|---|---|
| `net/mac80211` (imported) | GPL-2.0-only |
| `net/wireless` (imported) | GPL-2.0-only |
| `components/` (written here) | BSD-3-Clause |
| `tools/`, `docs/` (written here) | BSD-3-Clause |

A binary that links the imported stack is GPL-2.0 regardless of the license of
the parts written here: the BSD-3-Clause components become part of a GPL-2.0
work when linked with it. Components written here stay BSD-3-Clause as source
and as a standalone build.

## Import direction

Imports come in; nothing goes back out to a tree that must stay free of
GPL-2.0. If a component here is ever useful elsewhere, the version to share is
the one without the imported stack linked in.
