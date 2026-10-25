# rtl8189fs/patches - record only (external vendor tree)

The rtl8189fs vendor tree is **not** vendored in this repository; its GPL-2.0
bytes and embedded firmware are consumed from the external tree named by the
build's `VENDOR_8189FS` at pin `13bbdbc`. This directory is the record channel
for a vendor fix:

* With no patch here, the build compiles the external pin unmodified.
* If a reproduced vendor defect needs a fix, the patch is recorded here and
  applied to a build-local copy; the external tree stays the pin. The fix is
  written in `git format-patch` form (English, one concern, `Signed-off-by`).
* The vendor bytes are never edited in place and no manifest of the external
  tree is carried here; the pin and licence are in `../PROVENANCE.md`.
