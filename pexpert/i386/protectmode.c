#include <stdint.h>

// GDT Entry layout
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

// Declare a basic 3-entry GDT (Null, Code, Data)
struct gdt_entry gdt[3];
struct gdt_ptr gp;

void init_gdt(void) {
    // 0x00: Null Descriptor
    gdt[0] = (struct gdt_entry){0, 0, 0, 0, 0, 0};

    // 0x08: Code Segment Descriptor (Base=0, Limit=4GB, Read/Exec, 32-bit)
    gdt[1] = (struct gdt_entry){0xFFFF, 0x0000, 0x00, 0x9A, 0xCF, 0x00};

    // 0x10: Data Segment Descriptor (Base=0, Limit=4GB, Read/Write, 32-bit)
    gdt[2] = (struct gdt_entry){0xFFFF, 0x0000, 0x00, 0x92, 0xCF, 0x00};

    gp.limit = (sizeof(struct gdt_entry) * 3) - 1;
    gp.base  = (uint32_t)&gdt;
}

void switch_to_protected_mode(void) {
    // Disable interrupts before making the jump
    __asm__ __volatile__ ("cli");

    // Load Global Descriptor Table
    __asm__ __volatile__ ("lgdt %0" : : "m"(gp));

    // Enable the Protection Enable (PE) bit in CR0 and perform Far Jump
    __asm__ __volatile__ (
        ".code16gcc\n\t"              // Tell assembler we are executing from a 16-bit segment
        "movl %%cr0, %%eax\n\t"
        "orl $1, %%eax\n\t"
        "movl %%eax, %%cr0\n\t"       // Protected mode is now enabled!
        
        // Far jump to flush prefetch queue and change CS to 32-bit segment (0x08)
        "ljmp $0x08, $1f\n\t"         
        
        ".code32\n\t"                 // Tell assembler the remaining instructions are 32-bit
        "1:\n\t"
        
        // Reload all data segment registers with the Data Segment Selector (0x10)
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "movw %%ax, %%ss\n\t"
        
        // Optional: Re-align or set up your 32-bit stack pointer here if needed
        // "movl $0x90000, %%esp\n\t" 
        :
        :
        : "eax"                       // Tell compiler EAX is clobbered
    );
}
