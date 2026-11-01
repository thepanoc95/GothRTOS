.syntax unified
.arm

.section .vectors, "ax"
.align 5
.global goth_arm_vectors
.type goth_arm_vectors, %object
goth_arm_vectors:
    ldr pc, =_start
    ldr pc, =goth_arm_fault_undef
    ldr pc, =goth_arm_fault_svc
    ldr pc, =goth_arm_fault_prefetch
    ldr pc, =goth_arm_fault_data
    ldr pc, =goth_arm_fault_reserved
    ldr pc, =goth_arm_fault_irq
    ldr pc, =goth_arm_fault_fiq
    .ltorg
.size goth_arm_vectors, . - goth_arm_vectors

.section .text.boot, "ax"
.global _start
.type _start, %function
_start:
    cpsid if

#ifdef GOTH_ARM32_BCM2835
    @ ARMv6 has no VBAR. Copy the 16-word vector table (branches plus their
    @ literal pool) down to address 0, where ARMv6 fetches exceptions.
    ldr r0, =goth_arm_vectors
    mov r1, #0
    mov r2, #16
1:
    ldr r3, [r0], #4
    str r3, [r1], #4
    subs r2, r2, #1
    bne 1b
#else
    ldr r0, =goth_arm_vectors
    mcr p15, 0, r0, c12, c0, 0      @ VBAR
    isb
#endif

    ldr sp, =__stack_top

    ldr r0, =__bss_start
    ldr r1, =__bss_end
    mov r2, #0
2:
    cmp r0, r1
    strlo r2, [r0], #4
    blo 2b

    bl kmain

3:
    b 3b
.size _start, . - _start

.section .text
.type goth_arm_context_switch, %function
.global goth_arm_context_switch
@ void goth_arm_context_switch(uint32_t **save_sp, uint32_t *next_sp)
@   r0 = save_sp, r1 = next_sp
goth_arm_context_switch:
    push {r4-r11, lr}
    sub sp, sp, #4                  @ keep the saved frame 8-byte aligned
    str sp, [r0]
    mov sp, r1
    add sp, sp, #4
    pop {r4-r11, pc}
.size goth_arm_context_switch, . - goth_arm_context_switch

.type goth_arm_task_trampoline, %function
.global goth_arm_task_trampoline
@ New task contexts start with the task pointer in r4.
goth_arm_task_trampoline:
    mov r0, r4
    bl goth_arm_task_start
3:
    b 3b
.size goth_arm_task_trampoline, . - goth_arm_task_trampoline

.type goth_arm_fault_undef, %function
goth_arm_fault_undef:
    b goth_arm_fault_undef
.size goth_arm_fault_undef, . - goth_arm_fault_undef

.type goth_arm_fault_svc, %function
goth_arm_fault_svc:
    b goth_arm_fault_svc
.size goth_arm_fault_svc, . - goth_arm_fault_svc

.type goth_arm_fault_prefetch, %function
goth_arm_fault_prefetch:
    b goth_arm_fault_prefetch
.size goth_arm_fault_prefetch, . - goth_arm_fault_prefetch

.type goth_arm_fault_data, %function
goth_arm_fault_data:
    b goth_arm_fault_data
.size goth_arm_fault_data, . - goth_arm_fault_data

.type goth_arm_fault_reserved, %function
goth_arm_fault_reserved:
    b goth_arm_fault_reserved
.size goth_arm_fault_reserved, . - goth_arm_fault_reserved

.type goth_arm_fault_irq, %function
goth_arm_fault_irq:
    b goth_arm_fault_irq
.size goth_arm_fault_irq, . - goth_arm_fault_irq

.type goth_arm_fault_fiq, %function
goth_arm_fault_fiq:
    b goth_arm_fault_fiq
.size goth_arm_fault_fiq, . - goth_arm_fault_fiq
