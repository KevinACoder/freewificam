# components/printf - divergences

Local differences against the vendored upstream bytes (pin `d3b9846`).

| File | Divergence |
|---|---|
| (none) | The tree is byte-identical to its pin; the sha256 contract is `MANIFEST.sha256`. Configuration is expressed at the compile rule (`PRINTF_DISABLE_SUPPORT_FLOAT`, `PRINTF_DISABLE_SUPPORT_EXPONENTIAL`) and the binding lives in the consumer, never in these bytes |