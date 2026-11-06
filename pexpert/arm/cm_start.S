/* Cortex-M (ARMv6-M / ARMv8-M) startup and context switch for the GothRTOS
 * bare-metal port on the Raspberry Pi Pico (RP2040) and Pico 2 (RP2350).
 *
 * The vector table's first word is the initial stack pointer and the second is
 * the reset handler, which is what the boot ROM jumps to after loading the
 * image. The SYSGOTH bootloader enters the SparseKernel image at its entry
 * point (the reset vector), so this is also the image entry.
 *
 * Only Thumb-1 instructions are used so the same source builds for the
 * Cortex-M0+ (ARMv6-M) and the Cortex-M33 (ARMv8-M). */

.syntax unified
.thumb

.section .vectors, "a"
.align 2
.global goth_cm_vectors
.type goth_cm_vectors, %object
goth_cm_vectors:
    .word __stack_top
    .word _start
    .word goth_cm_fault_hang     /* NMI */
    .word goth_cm_fault_hang     /* HardFault */
    .word goth_cm_fault_hang     /* MemManage */
    .word goth_cm_fault_hang     /* BusFault */
    .word goth_cm_fault_hang     /* UsageFault */
    .word 0
    .word 0
    .word 0
    .word 0
    .word goth_cm_fault_hang     /* SVCall */
    .word goth_cm_fault_hang     /* DebugMonitor */
    .word 0
    .word goth_cm_fault_hang     /* PendSV */
    .word goth_cm_fault_hang     /* SysTick */
.size goth_cm_vectors, . - goth_cm_vectors

.section .text.boot, "ax"
.global _start
.type _start, %function
.thumb_func
_start:
    /* Disable interrupts, then clear .bss before entering C. */
    cpsid i
    ldr r0, =__bss_start
    ldr r1, =__bss_end
    movs r2, #0
1:
    cmp r0, r1
    bcs 2f
    str r2, [r0]
    adds r0, r0, #4
    b 1b
2:
    bl kmain
3:
    b 3b
.size _start, . - _start

.section .text
.thumb_func
.global goth_cm_context_switch
.type goth_cm_context_switch, %function
/* void goth_cm_context_switch(uint32_t **save_sp, uint32_t *next_sp)
 *   r0 = save_sp, r1 = next_sp
 * The saved frame (r4-r7, r8-r11, lr) matches goth_um_task_create. */
goth_cm_context_switch:
    push {r4-r7}
    mov r4, r8
    mov r5, r9
    mov r6, r10
    mov r7, r11
    push {r4-r7}
    mov r2, lr
    push {r2}
    sub sp, sp, #4
    mov r2, sp
    str r2, [r0]
    mov sp, r1
    add sp, sp, #4
    pop {r2}
    mov lr, r2
    pop {r4-r7}
    mov r8, r4
    mov r9, r5
    mov r10, r6
    mov r11, r7
    pop {r4-r7}
    bx lr
.size goth_cm_context_switch, . - goth_cm_context_switch

.section .text
.thumb_func
.global goth_cm_task_trampoline
.type goth_cm_task_trampoline, %function
/* New task contexts start with the task pointer in r4. */
goth_cm_task_trampoline:
    mov r0, r4
    bl goth_cm_task_start
4:
    b 4b
.size goth_cm_task_trampoline, . - goth_cm_task_trampoline

.section .text
.thumb_func
.type goth_cm_fault_hang, %function
goth_cm_fault_hang:
    b goth_cm_fault_hang
.size goth_cm_fault_hang, . - goth_cm_fault_hang
