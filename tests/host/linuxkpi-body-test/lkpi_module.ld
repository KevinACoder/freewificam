/* Linker fragment for the host build of the module-section walker.
 *
 * The board build brackets `.lkpi_module` in the platform linker script; the
 * host link has no such script, and GNU ld generates the __start_/__stop_
 * symbols only for sections it is told about. This fragment tells it: the
 * section keeps its own name and order, and the two boundary symbols land where
 * lkpi_usb_init() looks for them. KEEP is what stops --gc-sections from
 * discarding entries nothing references by address (the drivers keep their own
 * names; only the walker reaches them).
 *
 * INSERT AFTER .data places the section after initialised data, which is where
 * the board's own script puts it too (between the class-info table and .data).
 */
SECTIONS
{
  .lkpi_module : ALIGN(8)
  {
    __lkpi_module_start = .;
    KEEP(*(.lkpi_module))
    __lkpi_module_end = .;
  }
}
INSERT AFTER .data;
