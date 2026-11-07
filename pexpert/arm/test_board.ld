ENTRY(_start)

/* Test Board link script (scaffolded by OSBuild). */
SECTIONS
{
    . = 0x20000000;
    __image_start = .;

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
        *(.comment)
        *(.note*)
    }
}
