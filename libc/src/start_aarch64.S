/*
 * GothLibC process entry point for AArch64 Linux.
 */
    .text
    .globl _start
    .type _start, %function
_start:
    mov x29, #0
    mov x30, #0
    ldr x0, [sp]
    add x1, sp, #8
    add x2, x0, #1
    add x2, x1, x2, lsl #3
    bl goth_libc_start
    mov x8, #94
    svc #0
    brk #0
    .size _start, .-_start
    .section .note.GNU-stack,"",@progbits
