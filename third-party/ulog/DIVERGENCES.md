# components/ulog - divergences

Local differences against the vendored upstream bytes (pin `fc3b8c26`).

| File | Divergence |
|---|---|
| (none) | The tree is byte-identical to its pin; the sha256 contract is `MANIFEST.sha256`. Configuration is expressed at the compile rule (`ULOG_ENABLED`, `ULOG_MAX_MESSAGE_LENGTH=256`) and the binding lives in the consumer (`os/baremetal/log.c`), never in these bytes |

## Upstream observations (recorded, not patched)

* `src/ulog.h` declares the subscriber as `void (*)(ulog_level_t severity, char *msg)`
  (non-const `msg`) while the documentation example in the same header uses
  `const char *msg`. The declaration is authoritative for our subscriber signature; we
  match the header and leave the bytes alone.
* The message buffer is static and is overwritten by the next call; the header states
  this explicitly. The shell accounts for it by taking the print lock at the entry point.
