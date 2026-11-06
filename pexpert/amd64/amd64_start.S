/* AMD64 bare-metal entry, long-mode bring-up and context switch.
 *
 * The image is a multiboot1 ELF so QEMU can load it with `-kernel`. Multiboot
 * starts us in 32-bit protected mode with paging off; we build identity page
 * tables, enter long mode and jump to kmain. This mirrors the ARM32 port's
 * amd64_start/arm32_start split: only the CPU setup and context switch differ,
 * the scheduling contract is the same. */

.set MB_MAGIC, 0x1BADB002
.set MB_FLAGS, 0x00010003           /* page-align, mem info, a.out kludge */
.set MB_CHECKSUM, -(MB_MAGIC + MB_FLAGS)

.section .multiboot, "a"
.align 4
.global goth_amd64_multiboot_header
goth_amd64_multiboot_header:
    .long MB_MAGIC
    .long MB_FLAGS
    .long MB_CHECKSUM
    /* a.out kludge (MULTIBOOT_HEADER_HAS_ADDR). QEMU's loader refuses a
       multiboot ELF64 image, so we describe the flat binary layout instead:
       it loads [load_addr, load_end_addr) from the file and zeroes up to
       bss_end_addr before jumping to entry_addr. */
    .long goth_amd64_multiboot_header   /* header_addr */
    .long 0x00100000                    /* load_addr */
    .long __load_end                    /* load_end_addr (end of .data) */
    .long __bss_end                     /* bss_end_addr */
    .long _start                        /* entry_addr */

.section .text.boot, "ax"
.code32
.global _start
.type _start, %function
_start:
    cli
    /* Multiboot leaves the information structure pointer in ebx. */
    mov %ebx, goth_amd64_multiboot_info
    mov $boot_stack_top, %esp

    /* Zero .bss (the page tables are filled explicitly, the rest is not). */
    mov $__bss_start, %edi
    mov $__bss_end, %ecx
    sub %edi, %ecx
    xor %eax, %eax
    rep stosb

    /* PML4[0] -> PDPT, PDPT[0] -> PD. */
    mov $pdpt, %eax
    or $3, %eax
    mov %eax, pml4
    mov $pd, %eax
    or $3, %eax
    mov %eax, pdpt

    /* Fill the page directory with 512 2 MiB pages: identity-map 1 GiB. */
    mov $pd, %edi
    xor %ecx, %ecx
    xor %eax, %eax
1:
    mov %eax, %edx
    or $0x83, %edx                  /* present, writable, page size */
    mov %edx, (%edi, %ecx, 8)
    add $0x200000, %eax
    inc %ecx
    cmp $512, %ecx
    jne 1b

    mov $pml4, %eax
    mov %eax, %cr3

    /* Enable PAE. */
    mov %cr4, %eax
    or $0x20, %eax
    mov %eax, %cr4

    /* Set LME in EFER. */
    mov $0xC0000080, %ecx
    rdmsr
    or $0x100, %eax
    wrmsr

    /* Enable paging, which activates long mode. */
    mov %cr0, %eax
    or $0x80000000, %eax
    mov %eax, %cr0

    lgdt gdt64_ptr
    ljmp $0x08, $goth_amd64_long_mode
.size _start, . - _start

.code64
.type goth_amd64_long_mode, %function
goth_amd64_long_mode:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs
    mov $boot_stack_top, %rsp
    xor %rbp, %rbp
    call kmain
1:
    cli
    hlt
    jmp 1b
.size goth_amd64_long_mode, . - goth_amd64_long_mode

.section .text
.code64
.type goth_amd64_context_switch, %function
.global goth_amd64_context_switch
/* void goth_amd64_context_switch(uint64_t **save_sp, uint64_t *next_sp)
 *   rdi = &previous->sp, rsi = next->sp
 * The pushed registers match the frame built in goth_um_task_create. */
goth_amd64_context_switch:
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
    mov %rsp, (%rdi)
    mov %rsi, %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp
    ret
.size goth_amd64_context_switch, . - goth_amd64_context_switch

.type goth_amd64_task_trampoline, %function
.global goth_amd64_task_trampoline
/* New task contexts start with the task pointer in r12. */
goth_amd64_task_trampoline:
    mov %r12, %rdi
    call goth_amd64_task_start
1:
    cli
    hlt
    jmp 1b
.size goth_amd64_task_trampoline, . - goth_amd64_task_trampoline

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
    .space 16384
boot_stack_top:

.section .data
.global goth_amd64_multiboot_info
goth_amd64_multiboot_info:
    .quad 0
