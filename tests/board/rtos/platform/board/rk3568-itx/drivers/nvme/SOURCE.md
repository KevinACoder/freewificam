# nvme driver provenance

Imported from the reference RTOS tree at commit `19c5736` (documented only),
paths `board/rk3568-itx/drivers/nvme/`.

Files: `dwc_nvme.c/h`, `dwc_nvme_regs.h`, `nvme.h`.
License: BSD-3-Clause (SPDX lines present).
Local differences (this branch):
- `dwc_nvme_submit_async()` / `dwc_nvme_reap()` / `dwc_nvme_async_inflight()`:
  the asynchronous QD8 path with PRP lists (1 MiB) and a MDTS read from
  Identify Controller byte 77 (`dwc_nvme_max_transfer()`);
- `dwc_nvme_irq_intid()`: exposes the completion vector's INTID for the B0
  anchor.
The synchronous `Driver_NVME` face is unchanged; the two paths must not run at
the same time on one controller.
