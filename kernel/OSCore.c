#include "OSKernel.h"

typedef enum {
    TASK_SLOT_EMPTY,
    TASK_SLOT_READY,
    TASK_SLOT_BLOCKED,
    TASK_SLOT_EXITED
} TaskSlotState;

typedef struct {
    GothTaskStep step;
    void *context;
    TaskSlotState state;
} TaskSlot;

static TaskSlot tasks[GOTH_KERNEL_MAX_TASKS];
static uint8_t dispatch_cursor;

void GothKernelInit(void) {
    for (uint8_t index = 0; index < GOTH_KERNEL_MAX_TASKS; ++index) {
        tasks[index].step = 0;
        tasks[index].context = 0;
        tasks[index].state = TASK_SLOT_EMPTY;
    }
    dispatch_cursor = 0;
}

GothTaskId GothKernelAddTask(GothTaskStep step, void *context) {
    if (step == 0) {
        return GOTH_TASK_ID_INVALID;
    }

    for (uint8_t index = 0; index < GOTH_KERNEL_MAX_TASKS; ++index) {
        if (tasks[index].state == TASK_SLOT_EMPTY) {
            tasks[index].step = step;
            tasks[index].context = context;
            tasks[index].state = TASK_SLOT_READY;
            return index;
        }
    }

    return GOTH_TASK_ID_INVALID;
}

int GothKernelWakeTask(GothTaskId task_id) {
    if (task_id >= GOTH_KERNEL_MAX_TASKS ||
        tasks[task_id].state == TASK_SLOT_EMPTY ||
        tasks[task_id].state == TASK_SLOT_EXITED) {
        return 0;
    }

    tasks[task_id].state = TASK_SLOT_READY;
    return 1;
}

int GothKernelDispatchOne(void) {
    for (uint8_t offset = 0; offset < GOTH_KERNEL_MAX_TASKS; ++offset) {
        const uint8_t index = (dispatch_cursor + offset) % GOTH_KERNEL_MAX_TASKS;
        if (tasks[index].state != TASK_SLOT_READY) {
            continue;
        }

        dispatch_cursor = (index + 1) % GOTH_KERNEL_MAX_TASKS;
        const GothTaskResult result = tasks[index].step(tasks[index].context);
        if (result == GOTH_TASK_BLOCKED) {
            tasks[index].state = TASK_SLOT_BLOCKED;
        } else if (result == GOTH_TASK_EXITED) {
            tasks[index].state = TASK_SLOT_EXITED;
        } else if (result != GOTH_TASK_READY) {
            tasks[index].state = TASK_SLOT_BLOCKED;
        }
        return 1;
    }

    return 0;
}