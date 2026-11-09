#include "HardDriverFoundation.h"
#include "nanokernel.h"

#include <cassert>
#include <cstddef>

namespace {

char dispatch_order[8];
unsigned int dispatch_count;
unsigned int successful_starts;
unsigned int stops;

struct TaskState {
    char label;
    unsigned int calls;
    unsigned int block_on_call;
};

GothTaskResult record_step(void *context) {
    auto &task = *static_cast<TaskState *>(context);
    dispatch_order[dispatch_count++] = task.label;
    ++task.calls;
    return task.calls == task.block_on_call ? GOTH_TASK_BLOCKED : GOTH_TASK_READY;
}

GothTaskResult ready_step(void *) {
    return GOTH_TASK_READY;
}

HdfStatus fail_start(HdfDevice *) {
    return HDF_START_FAILED;
}

HdfStatus succeed_start(HdfDevice *) {
    ++successful_starts;
    return HDF_SUCCESS;
}

void stop_driver(HdfDevice *) {
    ++stops;
}

void test_scheduler(void) {
    GothKernelInit();
    dispatch_count = 0;
    TaskState first{'A', 0, 2};
    TaskState second{'B', 0, 1};
    const GothTaskId first_id = GothKernelAddTask(record_step, &first);
    const GothTaskId second_id = GothKernelAddTask(record_step, &second);

    assert(first_id != GOTH_TASK_ID_INVALID);
    assert(second_id != GOTH_TASK_ID_INVALID);
    assert(GothKernelDispatchOne());
    assert(GothKernelDispatchOne());
    assert(GothKernelDispatchOne());
    assert(dispatch_order[0] == 'A' && dispatch_order[1] == 'B' && dispatch_order[2] == 'A');
    assert(!GothKernelDispatchOne());

    assert(GothKernelWakeTask(first_id));
    assert(GothKernelWakeTask(second_id));
    assert(GothKernelDispatchOne());
    assert(dispatch_order[3] == 'B');

    GothKernelInit();
    assert(GothKernelAddTask(nullptr, nullptr) == GOTH_TASK_ID_INVALID);
    for (unsigned int index = 0; index < GOTH_KERNEL_MAX_TASKS; ++index) {
        assert(GothKernelAddTask(ready_step, nullptr) != GOTH_TASK_ID_INVALID);
    }
    assert(GothKernelAddTask(ready_step, nullptr) == GOTH_TASK_ID_INVALID);
}

void test_hdf(void) {
    HdfInitialize();
    successful_starts = 0;
    stops = 0;

    const HdfDevice device{"uart0", "test,uart", nullptr};
    const HdfDriver failing_driver{"failing", "test,uart", nullptr, fail_start, nullptr};
    const HdfDriver working_driver{"working", "test,uart", nullptr, succeed_start, stop_driver};

    assert(HdfRegisterDevice(&device) == HDF_SUCCESS);
    assert(HdfRegisterDevice(&device) == HDF_INVALID_ARGUMENT);
    assert(HdfRegisterDriver(&failing_driver) == HDF_SUCCESS);
    assert(HdfRegisterDriver(&working_driver) == HDF_SUCCESS);
    assert(HdfStartDrivers() == HDF_SUCCESS);
    assert(successful_starts == 1);
    HdfStopDrivers();
    assert(stops == 1);

    HdfInitialize();
    assert(HdfRegisterDevice(&device) == HDF_SUCCESS);
    assert(HdfStartDrivers() == HDF_NOT_FOUND);
}

}

int main() {
    test_scheduler();
    test_hdf();
}