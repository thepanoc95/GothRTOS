/* ELF entry is the 64-bit SYSGOTH/chainload entry. Multiboot (a.out kludge)
   jumps to _start (32-bit) via the header's entry_addr field. */
ENTRY(goth_amd64_sysgoth_entry)

SECTIONS
{
    . = 0x100000;                    /* 1 MiB, the conventional kernel load */

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
