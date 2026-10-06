# third-party/FreeRTOS-Kernel-tests - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/FreeRTOS/FreeRTOS.git (the FreeRTOS main repository, path `FreeRTOS/Test/CMock`) |
| Pin | commit `f4fcc3b228643144727e9257ba12db1cb632b6e6` (2026-08-26, the upstream `main` head at collection time) |
| Kernel coupling | the same commit records `FreeRTOS/Source` at `3a22924e0a9ddbbc8b0758881c33b3422a5cc20d` = the `third-party/FreeRTOS-Kernel` pin (V11.3.1): the tests and the kernel they exercise come from one upstream commit chain |
| License | MIT (`FreeRTOS/License` in the upstream repository; the tested sources carry the kernel tree's `LICENSE.md`) |
| Vendoring | plain file copy (vendored bytes), not a submodule; `CMock/` is the single submodule, pinned separately below |
| Fetched | 2026-10-07 |
| Collection | `git clone --depth 1 --filter=blob:none --sparse --branch main https://github.com/FreeRTOS/FreeRTOS.git`, then a copy of `FreeRTOS/Test/CMock/` excluding `CMock/`; the copy was verified with a recursive diff and a sha256 manifest (133 files) |
| Cross-check | byte-identical (same sha256 manifest, 133 files) to the frozen baseline copy of this tree at the same commit, so the collected bytes are independently confirmed |
| Local differences | none (zero patch); the Gate1 suite adapts through make command-line variables (`KERNEL_DIR`, `CC`, `BUILD_DIR`), never by editing these files |
| Consumers | `tools/mock/freertos-kernel-unit/` (Gate1 suite `freertos-kernel-unit`) |
| Reason | the official upstream unit tests for the kernel primitives (CMock/Unity): the kernel repository's own tags do not carry them, so the test tree is collected here and coupled to the kernel pin |
| Update flow | bump only together with `third-party/FreeRTOS-Kernel`: take the upstream commit whose `FreeRTOS/Source` gitlink equals the new kernel pin, re-copy the tree, re-verify the sha256 manifest, then update this file. The suite's preconditions assert the kernel gitlink, so a silent drift cannot pass unnoticed |

## CMock (nested submodule)

| Item | Value |
|---|---|
| Upstream | https://github.com/ThrowTheSwitch/CMock.git |
| Pin | commit `afa294982e8a28bc06f341cc77fd4276641b42bd` (`v2.5.2-16-gafa2949`) |
| Path | `third-party/FreeRTOS-Kernel-tests/CMock` |
| License | MIT (`LICENSE.txt`) |
| Vendoring | git submodule locked to the pin above |
| Nested pins | `vendor/unity` `cf949f45ca6d172a177b00da21310607b97bc7a7` (tag `v2.5.1`, MIT); `vendor/c_exception` `71b47be7c950f1bf5f7e5303779fa99a16224bb6` |

Notes:

* The two nested pins are recorded by CMock's own `.gitmodules` and index; `git submodule update
  --init --recursive` checks them out. They are not tracked by this repository.
* c_exception ships no `LICENSE.txt`; its license is stated in its README and source headers (MIT).
  Recorded as found.
* The vendored tree keeps upstream's own `.gitignore` (`build/`), so an in-tree default build stays
  out of git even when the suite is run without the `BUILD_DIR` override.
