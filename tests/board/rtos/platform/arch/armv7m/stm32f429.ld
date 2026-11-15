ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 192K
    /* The core board's W9825G6, FMC SDRAM bank 2.  NOLOAD sections only: the
     * startup code runs before main() initialises the controller. */
    SDRAM (rwx) : ORIGIN = 0xD0000000, LENGTH = 32M
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(2048);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH

    .ARM :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > FLASH

    /* USB host class registry: the hub and mass-storage class drivers publish
     * one struct usbh_class_info entry here and usbh_initialize() walks the
     * section between these two symbols to match an enumerated interface. */
    /* liblinux80211's registries (the same three sections the AArch64 script
     * defines in .text).  The wireless stack and the driver reach the image
     * only through these walks: a driver's struct usb_driver lives in
     * .lkpi_module, its module_init entry in .lkpi_init, and the SDIO
     * registry exists so an image that ever carries an SDIO driver keeps the
     * same contract.  Without the KEEP the linker garbage-collects every
     * entry, the walks run over empty sections, the driver is never
     * registered - and the wireless stack, referenced only by that driver,
     * is dropped from the image as well (an image that links but which has
     * no mac80211 symbol in it). */
    .lkpi_module : ALIGN(8)
    {
        __lkpi_module_start = .;
        KEEP(*(.lkpi_module))
        __lkpi_module_end = .;
    } > FLASH

    .lkpi_init : ALIGN(8)
    {
        __lkpi_init_start = .;
        KEEP(*(.lkpi_init))
        __lkpi_init_end = .;
    } > FLASH

    .lkpi_sdio_driver : ALIGN(8)
    {
        __lkpi_sdio_driver_start = .;
        KEEP(*(.lkpi_sdio_driver))
        __lkpi_sdio_driver_end = .;
    } > FLASH

    .usbh_class_info :
    {
        . = ALIGN(4);
        __usbh_class_info_start__ = .;
        KEEP(*(.usbh_class_info))
        __usbh_class_info_end__ = .;
        . = ALIGN(4);
    } > FLASH

    .board_tests :
    {
        . = ALIGN(4);
        __board_tests_start = .;
        KEEP(*(BOARD_TESTS))
        __board_tests_end = .;
        . = ALIGN(4);
    } > FLASH

    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT> FLASH

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /* The linuxkpi USB adapter's transfer stages.  On AArch64 this section is
     * the non-cacheable window; Cortex-M4 has no D-cache, so the section is
     * ordinary (uninitialised) RAM and exists only so the same adapter
     * compiles on both platforms. */
    .noncacheable (NOLOAD) :
    {
        . = ALIGN(64);
        __noncacheable_start = .;
        KEEP(*(.noncacheable))
        . = ALIGN(64);
        __noncacheable_end = .;
    } > RAM

    end = .;
    _end = .;

    /* FMC SDRAM (W9825G6, 32 MB).  NOLOAD: the startup .bss zeroing runs
     * before the controller is up.  The kernel heap (ucHeap) and the lwIP
     * heap reserve live here; the wireless profiles place both. */
    .sdram (NOLOAD) :
    {
        . = ALIGN(8);
        __sdram_start = .;
        KEEP(*(.sdram.heap))
        KEEP(*(.sdram))
        . = ALIGN(8);
        __lwip_heap = .;
        . += 256K;	/* lwIP MEM_SIZE reserve for the F429 wireless profiles */
        . = ALIGN(8);
        __sdram_end = .;
    } > SDRAM
}
