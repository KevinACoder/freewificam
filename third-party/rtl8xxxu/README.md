# rtl8xxxu

Byte-faithful export of the mainline Linux `rtl8xxxu` driver directory, kept on this
branch so a porting project can pin it as a submodule and diff nothing against a kernel
checkout.

## Contents

Nine `*.c` files, two `*.h` files, `Kconfig` and `Makefile` — the whole directory as it
stands in mainline Linux **7.3.0-rc2**, commit `e71d9a28760f605082308c2e1114627d406b4a29`
(`drivers/net/wireless/realtek/rtl8xxxu/`). No byte of any of these files is changed; the
consumer keeps `sha256sum` values for all thirteen.

The sources are flat at the branch root (the same layout as this fork's `main`
generation), so quoted includes such as `#include "regs.h"` resolve locally.

## Building it as a module

The extracted `Makefile` is the in-tree one; it builds the module when the kernel
configuration variable is set, which can also be passed on the command line for an
out-of-tree build against a prepared kernel build tree:

```
make -C /path/to/linux/build M=$PWD CONFIG_RTL8XXXU=m modules
```

No extra build shell is carried on this branch: a second `Makefile`/`Kbuild` would either
shadow the extracted file or duplicate its module list, and both would break the
byte-faithful claim above.

## License

GPL-2.0-only, per the `SPDX-License-Identifier` line of every file in this branch. The
driver is mainline kernel code; copyright remains with its authors. Consumers that
distribute a binary containing it take on the usual GPL obligations for that binary.
