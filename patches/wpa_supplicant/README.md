# patches/wpa_supplicant - index

Local patches replayed onto the vendored wpa_supplicant submodule
(`third-party/wpa_supplicant/`). The vendored tree itself is never edited: every
difference lives here and is applied at build time by
`tools/patches/replay.sh`.

| Item | Value |
|---|---|
| Upstream | https://github.com/KevinACoder/wpa_supplicant-rtos.git (branch `feat-embox-port`) |
| Base (= pin) | `0248409847056512eafae878a0a2260f10437b3c` |
| Patch list | `0001-events-compare-roam-thresholds-in-integer-arithmetic.patch` - `wpa_supplicant/events.c` compares estimated-throughput ratios against `1.2`/`1.5`/`1.1` literals; the patch rewrites them as exact integer ratios (`*6/5`, `*3/2`, `*11/10`) so the file also builds on targets without floating-point support (`-mgeneral-regs-only` images carry no FP state across calls) |
| Submission status | **not submitted** (no GitHub credentials in this environment; the patch is written in `git format-patch` form and is ready to open against the fork's `feat-embox-port` branch) |

Why this patch exists:

* The first-party adapter compiles the vendored units for images built without floating-point
  support. The float literals in the roam heuristic are the only such usage in the compile set;
  integer ratios express the same thresholds exactly.
* The `u`-suffixed unsigned arithmetic wraps nowhere near the value ranges involved
  (estimated throughput is reported in kbit/s), so the rewrite is behaviour-preserving for the
  inputs this code sees.

Rules (project patch policy):

* One concern per patch, `git format-patch` style, English, `Signed-off-by`, no
  local knowledge; written so it could be sent upstream as-is.
* New capability is added as new files rather than edits to upstream files
  wherever that is possible.
* When upstream accepts a patch: remove it here, move the pin forward, and note
  both in `third-party/wpa_supplicant.provenance.md`.
