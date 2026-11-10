# The active RTOS build configuration: the defconfig name.  The values live in
# tests/board/rtos/configs/<name>.defconfig and are rendered into
# build/kconfig/<name>/freewificam.config by tools/build/kconfig-gen.py.
# `./make.rtos list` lists the names; `./make.rtos config` regenerates the
# fragment.  The defconfig also selects the board (CONFIG_BOARD inside it).
CONFIG := rk3568-tick
