ENTRY(_start)

SECTIONS
{
    . = 0x400000;                    /* 4 MiB: above 1 MiB kernel load area */

    __image_start = .;

    .multiboot : ALIGN(4)
    {
        KEEP(*(.multiboot))
    }

    .text : ALIGN(4096)
    {
        KEEP(*(.text.boot))
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(8);
    }

    .data : ALIGN(8)
    {
        *(.data .data.*)
        . = ALIGN(8);
    }

    __load_end = .;

    .bss (NOLOAD) : ALIGN(4096)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    }

    . = ALIGN(8);
    __image_end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
