# components/net80211 - divergences

Deviations of this component from its upstream pin. The bytes of every kept file are
identical to the pin; the entries below are about the **trim** (files not kept) and
about the machinery the port took over.

| # | File(s) | Change | Reason | Round |
|---|---|---|---|---|
| T-1 | 8 `net80211/` sources | retired (see PROVENANCE, retired set) | outside the compile and link closure | M0-8b |
| T-2 | 39 `compat/` fat headers | retired | outside the closure under both include disciplines (driver, USB, PCI and toolchain vocabulary) | M0-8b |
| T-3 | 10 `compat/` machinery headers | retired; `sys/{mutex,intr,atomic,once,systm,kernel,cpu,proc}.h` replaced by the port's thin headers, `sys/cdefs.h` and `sys/errno.h` by the port's forwarding shims | the port owns the OS service face so one CMSIS-RTOS2 implementation serves every backend; the two shims must forward to the host/target header (the component's copies shadowed it) | M0-8b |
| T-4 | `port/osal/**`, `port/net/**`, `port_config*.h` | retired | the FreeRTOS OSAL and the embox shells are replaced by `ports/net80211/{osal,net}`; the harness config is replaced by the port prelude | M0-8b |
| T-5 | `tests/**` | retired | bound to the retired embox mbuf shell; `tests/host/net80211-port/` supersedes it | M0-8b |
| T-6 | `sys/malloc.h`, `sys/kmem.h` | kept (reclassified from machinery to kept vocabulary) | they only declare the port's allocation API (`wlan_kmalloc`, `kmem_*`) and map the BSD flags; the implementation is the port's - so they stay byte-identical here | M0-8b |

Import state (2026-10-07): all 61 kept files are byte-identical to
`9770b1ad7093f43a0f2ab2dc8d980067fb0f62a1` (`sha256sum -c MANIFEST.sha256`, 61/61);
no patch is applied.

Recovery rule: anything retired here comes back from the pin (the tag is public) when
its consumer appears (the supplicant's ioctl path -> `ieee80211_ioctl.c`, the TKIP/WEP
ciphers, a bus that needs the USB vocabulary), and that retrieval is recorded as a new
entry with the consuming round.

Rules for future changes:

* Prefer an upstreamable patch under `patches/net80211/` over an in-tree edit.
* If an in-tree edit is unavoidable, keep it minimal, mark it with
  `/* NET80211_PORT(L): <what and why> */` and add a row here.
* Port-side deviations (the machinery headers, the shims, the shells) are recorded in
  `ports/net80211/DIVERGENCES.md`, not here.
