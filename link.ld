/* link.ld - Linker script for the 64-bit kernel
 *
 * The kernel is loaded at physical address 0x100000 (1 MB) by the bootloader.
 * We use a higher-half identity map for now (virtual == physical).
 *
 * Sections layout:
 *   .text   - executable code
 *   .rodata - read-only data (string literals, const arrays)
 *   .data   - initialized read-write data
 *   .bss    - zero-initialized data (zeroed by boot_entry.asm:zero_bss)
 */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x100000;       /* load address: 1 MB */

    .text ALIGN(4096) :
    {
        *(.text.boot)   /* boot_entry.asm goes first */
        *(.text*)
    }

    .rodata ALIGN(4096) :
    {
        *(.rodata*)
    }

    .data ALIGN(4096) :
    {
        *(.data*)
    }

    /* BSS: zero-initialized. boot_entry.asm clears this via zero_bss. */
    .bss ALIGN(4096) :
    {
        bss_start = .;
        *(.bss*)
        *(COMMON)
        bss_end = .;
    }

    /* Discard unnecessary ELF sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
