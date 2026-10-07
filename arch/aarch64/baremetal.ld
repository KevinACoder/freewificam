/*
 * @file   baremetal.ld
 * @brief  Link script for the flat raw-binary image loaded by U-Boot.
 *
 * The image is loaded with `tftp 0xa000000 freertos.bin` and entered with
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

/* Two load segments, flags stated explicitly: code+rodata read/execute,
 * data+bss read/write. Without this the linker folds the script's sections
 * into one segment and warns about RWX permissions - a segment property the
 * raw binary output does not even carry. */
PHDRS
{
	text PT_LOAD FLAGS(5);
	data PT_LOAD FLAGS(6);
}

SECTIONS
{
	. = RAM_BASE;

	.text : {
		KEEP(*(.text.boot))
		KEEP(*(.vectors))
		KEEP(*(.freertos_vectors))
		*(.text .text.*)
	} :text

	.rodata : ALIGN(8) {
		*(.rodata .rodata.*)
		*(.data.rel.ro .data.rel.ro.*)
	} :text

	/* CherrySH command and variable tables: the shell walks these sections to
	 * find its commands, and nothing references the entries by address - so
	 * without KEEP the linker garbage-collects every one of them and the
	 * REPL reports "command not found" for the whole table. SUBALIGN(8)
	 * forces the entries dense: the core iterates the section as a flat array
	 * with a fixed stride, and padding between entries would hide every entry
	 * after the first (measured on the host build of the same tables). */
	.fsymtab : ALIGN(8) SUBALIGN(8) {
		. = ALIGN(4);
		__fsymtab_start = .;
		KEEP(*(FSymTab))
		__fsymtab_end = .;
	} :text

	.vsymtab : ALIGN(8) SUBALIGN(8) {
		. = ALIGN(4);
		__vsymtab_start = .;
		KEEP(*(VSymTab))
		__vsymtab_end = .;
	} :text

	.data : ALIGN(8) {
		__data_start = .;
		*(.data .data.*)
		__data_end = .;
	} :data

	.bss (NOLOAD) : ALIGN(16) {
		__bss_start = .;
		*(.bss .bss.*)
		*(COMMON)
		. = ALIGN(16);
		__bss_end = .;
	} :data

	/* Boot stack (the MMU page tables are plain .bss arrays, not a
	 * separate section - bss is NOLOAD, so the reservation is free). */
	. = ALIGN(16);
	__stack_bottom = .;
	. = . + 64K;
	__stack_top = .;

	/* End of everything this image statically owns. cache_init() drains the
	 * loader's dirty lines over [_start, __heap_end), so this symbol is the
	 * boot-hygiene extent. The RTOS heap is a .bss array (heap_4), not this
	 * window - the reservation only marks the extent and leaves room for
	 * future allocators. NOLOAD, costs nothing in the binary. */
	. = ALIGN(16);
	__heap_start = .;
	. = . + 1M;
	__heap_end = .;

	/DISCARD/ : {
		*(.comment)
		*(.note*)
		*(.eh_frame*)
	}

	/* Guard: the image must fit the usable RAM window. */
	ASSERT(. <= RAM_TOP, "image exceeds the usable RAM window")
}
