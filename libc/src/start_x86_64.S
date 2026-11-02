/*
 * GothLibC process entry point for x86-64 Linux.
 *
 * The initial stack holds argc, argv, envp and the auxiliary vector.  Only
 * argc/argv/envp are needed: the heap is supplied by goth_libc_start().
 */
    .text
    .globl _start
    .type _start, @function
_start:
    xor %rbp, %rbp
    mov (%rsp), %rdi
    lea 8(%rsp), %rsi
    lea 16(%rsp,%rdi,8), %rdx
    and $-16, %rsp
    call goth_libc_start
    mov %eax, %edi
    mov $60, %eax
    syscall
    hlt
    .size _start, .-_start
    .section .note.GNU-stack,"",@progbits
