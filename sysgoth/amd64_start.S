/* SYSGOTH AMD64 boot entry.
 *
 * Multiboot1 image loaded by QEMU (-kernel) or a multiboot bootloader.
 * Brings the CPU up from 32-bit protected mode to long mode and calls
 * sysgoth_amd64_main. Linked at 4 MiB so it does not overlap kernels that
 * load at 1 MiB. */

.set MB_MAGIC, 0x1BADB002
.set MB_FLAGS, 0x00010003           /* page-align, mem info, a.out kludge */
.set MB_CHECKSUM, -(MB_MAGIC + MB_FLAGS)

.section .multiboot, "a"
.align 4
.global sg_amd64_multiboot_header
sg_amd64_multiboot_header:
    .long MB_MAGIC
    .long MB_FLAGS
    .long MB_CHECKSUM
    .long sg_amd64_multiboot_header /* header_addr */
    .long 0x00400000                /* load_addr (4 MiB) */
    .long __load_end
    .long __bss_end
    .long _start                    /* entry_addr */

.section .text.boot, "ax"
.code32
.global _start
.type _start, %function
_start:
    cli
    mov $boot_stack_top, %esp

    /* Zero .bss. */
    mov $__bss_start, %edi
    mov $__bss_end, %ecx
    sub %edi, %ecx
    xor %eax, %eax
    rep stosb

    /* Identity-map the first 1 GiB with 2 MiB pages. */
    mov $pdpt, %eax
    or $3, %eax
    mov %eax, pml4
    mov $pd, %eax
    or $3, %eax
    mov %eax, pdpt

    mov $pd, %edi
    xor %ecx, %ecx
    xor %eax, %eax
1:
    mov %eax, %edx
    or $0x83, %edx
    mov %edx, (%edi, %ecx, 8)
    add $0x200000, %eax
    inc %ecx
    cmp $512, %ecx
    jne 1b

    mov $pml4, %eax
    mov %eax, %cr3

    mov %cr4, %eax
    or $0x20, %eax              /* PAE */
    mov %eax, %cr4

    mov $0xC0000080, %ecx
    rdmsr
    or $0x100, %eax             /* LME */
    wrmsr

    mov %cr0, %eax
    or $0x80000000, %eax        /* PG */
    mov %eax, %cr0

    lgdt gdt64_ptr
    ljmp $0x08, $sg_amd64_long_mode
.size _start, . - _start

.code64
.type sg_amd64_long_mode, %function
sg_amd64_long_mode:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs
    mov $boot_stack_top, %rsp
    xor %rbp, %rbp
    call sysgoth_amd64_main
1:
    cli
    hlt
    jmp 1b
.size sg_amd64_long_mode, . - sg_amd64_long_mode

.section .rodata
.align 16
gdt64:
    .quad 0
    .quad 0x00AF9A000000FFFF        /* 64-bit ring-0 code */
    .quad 0x00CF92000000FFFF        /* ring-0 data */
gdt64_ptr:
    .word gdt64_ptr - gdt64 - 1
    .quad gdt64

.section .bss
.align 4096
pml4:
    .space 4096
pdpt:
    .space 4096
pd:
    .space 4096
.align 16
boot_stack:
    .space 32768
boot_stack_top:
