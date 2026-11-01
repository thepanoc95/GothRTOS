#ifndef GOTHRTOS_ARM32_H
#define GOTHRTOS_ARM32_H

/* Helpers provided by the ARM32 bare-metal port. */

void goth_arm32_exit(int status) __attribute__((noreturn));
void goth_arm32_cpu_relax(void);

#endif
