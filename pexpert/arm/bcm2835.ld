ENTRY(_start)

SECTIONS
{
    /* The Raspberry Pi firmware loads kernel.img at 0x8000. */
    . = 0x8000;
    __image_start = .;

    .text : ALIGN(4)
    {
        KEEP(*(.vectors))
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
