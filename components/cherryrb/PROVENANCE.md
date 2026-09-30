# PROVENANCE — components/cherryrb (vendored CherryRB)

- Upstream: https://github.com/cherry-embedded/CherryRB
- Pin: `19ea7c6efcf19dc9e805a0a662212533ee5b1edb` (`git describe` = `v1.0.0`)
- Import date: 2026-10-01
- License: Apache-2.0 (see ./LICENSE, kept verbatim)
- Form: vendored byte-identical (whole upstream tree is these files)
- Provenance chain: byte-verified against the prior project's pinned
  submodule clone (`~/workspace/project_web_camera/freewebcamera/third-party/
  cherryrb` @ the same pin, per its IMPORT-INFO.md "adapter-internal glue").

## Imported subset

`chry_ringbuffer.c`, `chry_ringbuffer.h`, `LICENSE`, `README.md`,
`CHANGELOG.md` - that IS the whole upstream tree.

## Zero-difference declaration

No byte differs from the upstream pin.

## Usage

Console RX ring between the UART RX interrupt (single producer) and the
shell task (single consumer). SPSC is a relied-on contract, not an
assumption - exactly one ISR writes, exactly one task reads
(port/adapters/cherrysh/cherrysh_adapter.c). CherryRB has NO memory
barriers of its own (its documentation says so); the ordering
(ring write -> dsb sy -> task wake) lives in the adapter.

## Promotion back to submodule

Same procedure as components/cherrysh/PROVENANCE.md; pin
`19ea7c6efcf19dc9e805a0a662212533ee5b1edb`.
