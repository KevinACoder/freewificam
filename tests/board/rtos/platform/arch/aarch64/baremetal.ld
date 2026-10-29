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

/* Board override hooks: an image for another load window passes
 * --defsym=board_ram_base/-top on the link line and wins; the RK3568 numbers
 * stay the default so every existing build is byte-identical. The firefly v1
 * image links at 0x90000000 with its top at 0xc0000000, the
 * window the lab profile loads the flat binary into. */
RAM_BASE = DEFINED(board_ram_base) ? board_ram_base : 0x0a000000;
RAM_TOP  = DEFINED(board_ram_top)  ? board_ram_top  : 0x10000000;

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

	/* Board regression test table: the suite's runner walks this section as a
	 * flat array with a fixed stride (entries are {name, fn} - two pointers),
	 * so the shell tables' KEEP + SUBALIGN(8) discipline applies unchanged:
	 * without KEEP the linker garbage-collects every entry, and padding would
	 * hide every entry after the first. The image's `make check` asserts the
	 * byte count equals entries x 16. */
	.board_tests : ALIGN(8) SUBALIGN(8) {
		. = ALIGN(8);
		__board_tests_start = .;
		KEEP(*(BOARD_TESTS))
		__board_tests_end = .;
	} :text

	/* USB host class registry: the stack's class drivers publish a
	 * struct usbh_class_info entry here (the hub and mass-storage classes
	 * of this image), and usbh_initialize() walks the section between
	 * these two symbols to match an enumerated interface against a class
	 * driver. Nothing references the entries by address, so the KEEP
	 * discipline of the shell and test tables applies unchanged; the
	 * entries are declared byte-aligned, so no SUBALIGN is needed. */
	.usbh_class_info : ALIGN(8) {
		__usbh_class_info_start__ = .;
		KEEP(*(.usbh_class_info))
		__usbh_class_info_end__ = .;
	} :text

	/* Linux driver registry: `module_usb_driver()` (the Linux module
	 * registration macro's spelling in the linuxkpi contract header) puts
	 * each registered struct usb_driver * here, and lkpi_usb_init() walks
	 * the section between these two symbols to register them, exactly the
	 * way the kernel's module loader would. Nothing references an entry by
	 * address (each driver object keeps its own name), so the KEEP
	 * discipline of the class-info table applies unchanged; the entries are
	 * natural `void *` alignments, so no SUBALIGN is needed. The fixed step
	 * makes the section's byte count a check: entries x 8. */
	.lkpi_module : ALIGN(8) {
		__lkpi_module_start = .;
		KEEP(*(.lkpi_module))
		__lkpi_module_end = .;
	} :text

	/* Module init entries: `module_init()` (the same contract header) puts
	 * each driver's entry point here, and lkpi_module_init_run() walks the
	 * section between these two symbols and runs them in section order, once.
	 * It is deliberately a separate section from .lkpi_module above, so the
	 * two walks cannot disturb each other's byte layout; the entries are
	 * function pointers, so the natural alignment is the same fixed step and
	 * the byte count is entries x 8. */
	.lkpi_init : ALIGN(8) {
		__lkpi_init_start = .;
		KEEP(*(.lkpi_init))
		__lkpi_init_end = .;
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
	} :data

	/* Non-cacheable DMA window: everything marked with the stack's
	 * non-cacheable section attribute lands here, and mmu.c maps exactly
	 * [__noncacheable_start, __noncacheable_end) with the non-cacheable
	 * memory attribute. Three placement rules, all load-bearing:
	 *
	 *  - it sits inside the [__bss_start, __bss_end) span so the boot
	 *    clear zeroes it like any other static (the controller drivers'
	 *    pools are allocated by taking a slot from a zeroed array);
	 *  - it is 2M aligned and the assertion below caps it at one 2M block,
	 *    because the identity map describes the image's RAM with 1GiB L1
	 *    and 2M L2 blocks and the arch layer reserves exactly ONE page
	 *    table to refine that block into 4K pages;
	 *  - it is NOLOAD: the window reserves address space, not file
	 *    content, so the raw image the loader copies does not grow.
	 *
	 * One window serves every bus, not one per controller: the drivers'
	 * pools are [bus][...] arrays sized by the bus count, so a per-bus
	 * window would have to slice arrays that the drivers index
	 * themselves. The window is sized by what is in it - the assertion
	 * below is the check, and the pools' total is far below the 2M cap
	 * (the transfer rings dominate at a few hundred KiB). */
	.noncacheable (NOLOAD) : ALIGN(2M) {
		__noncacheable_start = .;
		*(.noncacheable)
		*(.noncacheable.*)
		. = ALIGN(4K);
		__noncacheable_end = .;
	} :data

	. = ALIGN(16);
	__bss_end = .;

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
	 * future allocators. NOLOAD, costs nothing in the binary.
	 *
	 * The window's size is the image's to state, because how much a heap
	 * needs is a property of the drivers in it: an image whose driver holds
	 * multi-kilobyte receive buffers asks for more than the default through
	 * --defsym=HEAP_SIZE=..., and the default stays what the first image on
	 * this carrier needed. */
	PROVIDE(HEAP_SIZE = 1M);
	. = ALIGN(16);
	__heap_start = .;
	. = . + HEAP_SIZE;
	__heap_end = .;

	/* The ThreadX cortex_a55 port dereferences this symbol during kernel
	 * init (tx_initialize_low_level.S stores *__top_of_ram into
	 * _tx_initialize_unused_memory). The dereferenced value is never
	 * consumed by this image's allocators - the symbol only has to resolve
	 * to a mapped address; a missing definition is a link error. */
	PROVIDE(__top_of_ram = __heap_end);

	/DISCARD/ : {
		*(.comment)
		*(.note*)
		*(.eh_frame*)
	}

	/* Guard: the image must fit the usable RAM window. */
	ASSERT(. <= RAM_TOP, "image exceeds the usable RAM window")

	/* Guard: the non-cacheable DMA window must stay inside the single 2M
	 * block the arch layer maps it with (mmu.c reserves one page table
	 * for it). Growing the window past this is a link error, not a
	 * runtime surprise. */
	ASSERT(__noncacheable_end - __noncacheable_start <= 2M,
	       "the non-cacheable DMA window outgrew the 2M MMU block that maps it")
}
