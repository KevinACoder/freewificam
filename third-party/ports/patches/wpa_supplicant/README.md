# wpa_supplicant-rtos import (PROVENANCE)

- **upstream**: https://github.com/KevinACoder/wpa_supplicant-rtos
  (fork of NXP `mcuxsdk-middleware-wireless-wireless-wpa_supplicant-rtos`
  lineage; hostap 2.11-devel base)
- **pin**: `0248409847056512eafae878a0a2260f10437b3c` (upstream commit;
  the prior project materialized its child `151e61c` on the same pin —
  see specs/wpa-supplicant-rtos-adapt/SPEC.md §3.1)
- **added**: 2026-10-02 (P7a-1, feat/wpa-s7a-r1)
- **license**: BSD-3 (`LICENSE` in the submodule root). BSD-3 is the
  reason this component line merges to **trunk** (spec §3.7), unlike
  the GPL vendor driver lines which stay feat-only.
- **materialization**: build-time patch replay by `make modules`
  (`third-party/ports/patches/wpa_supplicant/*.patch` onto the pinned work tree);
  idempotent (an applied patch fails `apply --check` and is kept).
  The submodule work tree is never committed.
- **patches**:
  - `0001-events-roam-compare-in-integers.patch` — inherited verbatim
    from the prior project (`third-party/ports/patches/wpa_supplicant/` there, 44 lines):
    events.c roam-estimation float compares become integer math
    (`*1.2/*1.5/*1.1` → `*6/5, *3/2, *11/10`), because the image builds
    `-mgeneral-regs-only` and saves no FP state.
- **build set**: PSK-only 45 vendored units (Makefile `WPA_CORE_SRCS`,
  list inherited from the prior project's Makefile:832-877) + the
  first-party adaptation in `os/wpa_supplicant/`. The in-tree
  CMakeLists/Kconfig (MCUXpresso component form) is not used.
