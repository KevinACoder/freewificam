/*
 * @file   baremetal.ld
 * @brief  Link script for the flat raw-binary image loaded by U-Boot.
 *
 * The image is loaded with `tftp 0xa000000 rtos.bin` and entered with
 * `go 0xa000000` (bootelf is unusable on this board - it faults at the jump
 * and its ELF path copies no segments anyway). The load address must match
 * what the boot profile uses.
 *
 * RAM window is [0x0a000000, 0x10000000): below 0x0a000000 belongs to
 * firmware (ATF/OP-TEE); at or above 0x10000000 an access raises an external
 * abort.
 *
 * Section order is load-bearing: .text.boot MUST come first because
 * `go 0xa000000` starts executing at the load address without consulting any
 * ELF header. Placing the vector table first meant the first instruction
 * executed was a branch into the fault handler, which parked before the
 * console existed - the image loaded, ran, and printed nothing. The vectors
 * are located by pointer (VBAR_EL1 loads the boot_vectors symbol), so they
 * do not need offset 0.
 */

ENTRY(_start)

RAM_BASE = 0x0a000000;
RAM_TOP  = 0x10000000;

SECTIONS
{
	. = RAM_BASE;

	.text : {
		KEEP(*(.text.boot))
		KEEP(*(.vectors))
		*(.text .text.*)
	}

	.rodata : ALIGN(8) {
		*(.rodata .rodata.*)
		*(.data.rel.ro .data.rel.ro.*)
	}

	.data : ALIGN(8) {
		__data_start = .;
		*(.data .data.*)
		__data_end = .;
	}

	/* CherrySH command and variable tables (S4/cherrysh-adapt §3.2.2).
	 * Exported command entries are never referenced by a direct call -
	 * the shell finds them by walking the section - so without KEEP the
	 * linker garbage-collects every one of them and the REPL reports
	 * "command not found" for the whole table. */
	. = ALIGN(8);
	__fsymtab_start = .;
	.fsymtab : { KEEP(*(FSymTab)) }
	__fsymtab_end = .;
	. = ALIGN(8);
	__vsymtab_start = .;
	.vsymtab : { KEEP(*(VSymTab)) }
	__vsymtab_end = .;

	/* CherryUSB host class registry (S5/cherryusb-xhci-port §3.3): the
	 * hub class (and later MSC) publishes struct usbh_class_info here
	 * (CLASS_INFO_DEFINE, usbh_core.h:76); usbh_initialize() walks the
	 * boundaries to match enumerated interfaces against class drivers.
	 * Same KEEP discipline as FSymTab - nothing references the entries
	 * by address. */
	. = ALIGN(8);
	__usbh_class_info_start__ = .;
	.usbh_class_info : { KEEP(*(.usbh_class_info)) }
	__usbh_class_info_end__ = .;

	.bss (NOLOAD) : ALIGN(16) {
		__bss_start = .;
		*(.bss .bss.*)
		*(COMMON)
		. = ALIGN(16);
		__bss_end = .;
	}

	/* Boot stack (the MMU page tables are plain .bss arrays, not a
	 * separate section - bss is NOLOAD, so the reservation is free). */
	. = ALIGN(16);
	__stack_bottom = .;
	. = . + 64K;
	__stack_top = .;

	/* Heap reservation: unused this milestone (no allocator), kept so the
	 * cache-hygiene range and the ThreadX-round "unused memory" pointer
	 * have one owner. NOLOAD, costs nothing in the binary. */
	. = ALIGN(16);
	__heap_start = .;
	. = . + 4M;
	__heap_end = .;

	/* First byte past everything the image statically reserves. Kernel-
	 * neutral name: the ThreadX round's _tx_initialize_unused_memory reads
	 * this ("start allocating here"). PROVIDE keeps it free unless asked
	 * for. */
	PROVIDE(__top_of_ram = __heap_end);

	/DISCARD/ : {
		*(.comment)
		*(.note*)
		*(.eh_frame*)
	}

	/* Guard: the image must fit the usable RAM window. */
	ASSERT(. <= RAM_TOP, "image exceeds the usable RAM window")
}
