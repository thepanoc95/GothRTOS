ENTRY(_start)

/* Cortex-M (Pico / Pico 2) link script.
 *
 * The image is linked for SRAM at 0x20000000 so it can be either flashed
 * directly or streamed by the SYSGOTH bootloader. Both RP2040 and RP2350 map
 * 264 KiB of SRAM there, and SRAM is executable on both parts, so a flat SRAM
 * image needs no XIP setup.
 *
 * The first word of the image is the initial stack pointer and the second is
 * the reset vector, so the entry point the bootloader jumps to is also the
 * reset handler. */

SECTIONS
{
    . = 0x20000000;
    __image_start = .;

    .vectors : ALIGN(4)
    {
        KEEP(*(.vectors))
        . = ALIGN(4);
    }

    .text : ALIGN(4)
    {
        KEEP(*(.text.boot))
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    }

    .data : ALIGN(4)
    {
        *(.data .data.*)
        . = ALIGN(4);
    }

    .bss (NOLOAD) : ALIGN(4)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    }

    .stack (NOLOAD) : ALIGN(8)
    {
        __stack_bottom = .;
        . = . + 0x4000;
        __stack_top = .;
    }

    . = ALIGN(8);
    __image_end = .;

    /DISCARD/ :
    {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.comment)
        *(.note*)
    }
}
