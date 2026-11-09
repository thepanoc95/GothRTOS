#ifndef GOTHRTOS_NANOKERNEL_H
#define GOTHRTOS_NANOKERNEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GOTH_KERNEL_MAX_TASKS 8
#define GOTH_TASK_ID_INVALID UINT8_MAX

typedef uint8_t GothTaskId;

typedef enum {
    GOTH_TASK_READY,
    GOTH_TASK_BLOCKED,
    GOTH_TASK_EXITED
} GothTaskResult;

typedef GothTaskResult (*GothTaskStep)(void *context);

void GothKernelInit(void);
GothTaskId GothKernelAddTask(GothTaskStep step, void *context);
int GothKernelWakeTask(GothTaskId task_id);
int GothKernelDispatchOne(void);

#ifdef __cplusplus
}
#endif

#endif