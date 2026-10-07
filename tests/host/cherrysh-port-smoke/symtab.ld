/* Command and variable table boundaries for the host link.
 *
 * CherrySH registers commands and variables through the FSymTab and VSymTab
 * sections (see csh.h in components/cherrysh). The upstream porting notes put
 * this snippet into the application linker script; the host build has no
 * linker script of its own, so this fragment is passed with -T and inserted
 * into the default script.
 *
 * The core reads both tables as flat, packed arrays; the explicit output
 * sections are what makes the insertion and the KEEP clauses legal, SUBALIGN
 * pins every input section to 8-byte packing so no linker-chosen alignment
 * can leave gaps between entries, and KEEP keeps the tables alive when the
 * link runs with --gc-sections. (The compiler side of the same rule is
 * -malign-data=abi in the Makefile: x86-64 GCC over-aligns these objects by
 * default, which would pad the tables even without a gap here.)
 */
SECTIONS
{
    .fsymtab : ALIGN(8) SUBALIGN(8)
    {
        . = ALIGN(4);
        __fsymtab_start = .;
        KEEP(*(FSymTab))
        __fsymtab_end = .;
    }
    .vsymtab : ALIGN(8) SUBALIGN(8)
    {
        . = ALIGN(4);
        __vsymtab_start = .;
        KEEP(*(VSymTab))
        __vsymtab_end = .;
    }
} INSERT AFTER .data;
