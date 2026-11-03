/*
 * Optional linker script for freestanding experiments with gothdroid.
 *
 * The supported Android build is a normal PIE executable produced by the NDK
 * (CMakeLists.txt does not use this file).  It exists so the runtime can also
 * be linked as a small self-contained image when someone supplies their own
 * startup code and libc:
 *
 *   cmake -DGOTHDROID_FREESTANDING=ON ...
 *
 * The layout keeps text and read-only data together and page-aligns the
 * writable segment, which is enough for a single flat image loaded by a
 * bootloader or a minimal ELF loader.
 */

ENTRY(main)

SECTIONS
{
    . = ALIGN(4096);

    .text : {
        *(.text.startup)
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    . = ALIGN(4096);

    .data : {
        *(.data .data.*)
    }

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }

    . = ALIGN(4096);
    __image_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
    }
}
