# known_issues - what a release is known to get wrong

Every release carries a register of the issues it is known to have: one file
per release, named after the tag (`v0.0.2.md`). The register is part of the
release, not a bug tracker: it says what does not work, how it shows, what the
impact is, and what a user can do about it. A release with an empty register is
a claim that nothing is known to be wrong, so an empty file is written
explicitly.

## Who this is for

Someone building an image or a module from this repository and running it on
the reference board. The entries describe behaviour they can see; they do not
describe internal work items.

## What each entry has

| Field | Meaning |
|---|---|
| `ID` | a stable, lowercase slug. It does not change when the wording does; a closed entry keeps its ID in the older files |
| `Area` | the line or subsystem (`rtw89`, `rtl8xxxu`, `iwlwifi`, `rtl8189fs`, `usb`, `camera`, `platform`, `rtos`) |
| `Affected` | the profiles, module arms or kernels where the issue shows |
| `Symptom` | what a user sees, in their terms |
| `Impact` | what it costs (a mode that cannot be used, a rate that is not reached, a boot that has to be repeated) |
| `Workaround` | what can be done about it today, or `none` |
| `Status` | `open`, `under investigation`, `workaround available`, `not reproducible`, `expected` (a declared scope limitation), `fixed in <version>` |
| `Severity` | `blocking` (the path is unusable), `major` (a significant degradation or an unreliable path), `minor` (narrow or avoidable), `informational` (a scope statement, not a defect) |
| `Observed` | how it was seen: the board, the scenario, the date. The measurement records themselves are not part of this repository |
| `Related` | the code or documentation a reader can start from |

## Rules

- A register is written for its release and does not change afterwards, except
  to mark an entry `fixed in <version>`. The next release starts by copying the
  file and re-checking every `open` entry against the new tree.
- An entry is not a promise: `under investigation` may stay that way, and
  `expected` entries are scope statements, not defects to be fixed.
- No internal identifiers, no private paths, no round names: an entry has to
  read correctly to someone who has never seen the development machine.
- Numbers in an entry carry their scenario (window, direction, tool), the same
  way the release notes do.

## Reporting

There is no separate tracker for this repository yet. A report is a plain
description: what was built, which board and part, what was done, what happened,
and the serial log if there is one. Open it where this repository is hosted and
name the release tag in the subject.
