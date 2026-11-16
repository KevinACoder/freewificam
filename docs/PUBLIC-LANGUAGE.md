# Public language

`freewificam` is the public side of the project. Its first-party sources,
profiles, tools and docs use descriptive stage names: **compile**, **link**,
**probe**, **attach**, **scan**, **association**, **data**, **throughput**.
The internal stage numbers from the private ledger are not part of the public
vocabulary.

`tools/build/check-public-language.py` enforces the rule on first-party files;
vendored and imported trees (third-party, wireless-drivers, the imported Linux
stack and firmware) are excluded. The 8189FTV case, profile and tool paths are
required to be clean.

A baseline in `tools/build/public-language-baseline.json` records the remaining
historical occurrences in unrelated lines; the checker fails on any new
occurrence or any increase. The baseline is debt, not permission: when a
cleanup removes debt, regenerate it; never add a new hit. The checker runs in
`make.rtos check` and therefore in the CI check.
