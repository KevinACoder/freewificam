# pcie driver provenance

Imported from the reference RTOS tree `~/workspace/project_rtos_webcam/freewificam`
(documented only; working tree at `19c5736` on `feat-rtw88-l0-baseline`), paths
`board/rk3568-itx/drivers/pcie/`.

Files: `pcie_dw.c/h`, `dwc_pcie.c/h`, `dwc_msix.c/h`, `dwc_msi.c/h`.
License: BSD-3-Clause (each file carries its SPDX line).
Local differences: none at import; the build integration is in
`tests/board/rtos/build/Makefile` (the `pcie` case group) and the cases are
first-party. The 7260 probe (`pcie_7260.c`) is new in this branch.
