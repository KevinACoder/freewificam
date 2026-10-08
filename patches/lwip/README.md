# patches/lwip - index

Local patches replayed onto the vendored lwIP submodule (`third-party/lwip/`).
The vendored tree itself is never edited: every difference lives here and is
applied at build time by `tools/patches/replay.sh`.

| Item | Value |
|---|---|
| Upstream | https://github.com/lwip-tcpip/lwip.git |
| Base (= pin) | `77dcd25a72509eb83f72b033d219b1d40cd8eb95` (tag `STABLE-2_2_1_RELEASE`) |
| Patch list | (empty) |
| Submission status | no patches to submit - the import is zero-patch by design: the OS coupling is the first-party `ports/lwip/` adapter over CMSIS-RTOS2, and every configuration knob the stack offers is used through `lwipopts.h` and `arch/cc.h`, so no upstream byte needs to change |

Rules (project patch policy):

* One concern per patch, `git format-patch` style, English, `Signed-off-by`, no
  local knowledge; written so it could be sent upstream as-is.
* New capability is added as new files rather than edits to upstream files
  wherever that is possible.
* When upstream accepts a patch: remove it here, move the pin forward, and note
  both in `third-party/lwip.provenance.md`.
