/* Linker fragment for the host build of the two module sections.
 *
 * The board build brackets both sections in the platform linker script; a host
 * link has no such script, and GNU ld emits the __start_/__stop_ symbols only
 * for sections it is told about. This fragment tells it about both: each keeps
 * its own name and order, and each pair of boundary symbols lands where its
 * walker looks for it. KEEP is what stops --gc-sections from discarding entries
 * nothing references by address (each driver keeps its own name; only the walker
 * reaches them).
 *
 * The two are separate output sections on purpose - that separation is the
 * property the suite asserts: the module_init channel must not change the usb
 * driver registry's byte layout. INSERT AFTER .data places them after
 * initialised data, which is where the board's own script puts them too.
 */
SECTIONS
{
  .lkpi_module : ALIGN(8)
  {
    __lkpi_module_start = .;
    KEEP(*(.lkpi_module))
    __lkpi_module_end = .;
  }

  .lkpi_init : ALIGN(8)
  {
    __lkpi_init_start = .;
    KEEP(*(.lkpi_init))
    __lkpi_init_end = .;
  }
}
INSERT AFTER .data;
