#include "gothrtos/kernel.h"

static bool endpoint_storage_ok(const goth_endpoint_t *endpoint)
{
    return endpoint != NULL && endpoint->storage != NULL &&
           endpoint->capacity != 0U && endpoint->head < endpoint->capacity &&
           endpoint->count <= endpoint->capacity;
}

bool goth_endpoint_valid(const goth_endpoint_t *endpoint)
{
    return endpoint_storage_ok(endpoint);
}

goth_status_t goth_endpoint_init(goth_endpoint_t *endpoint,
                                 goth_message_t *storage,
                                 size_t capacity)
{
    if (endpoint == NULL || storage == NULL || capacity == 0U) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    endpoint->storage = storage;
    endpoint->capacity = capacity;
    endpoint->head = 0U;
    endpoint->count = 0U;
    return GOTH_STATUS_OK;
}

size_t goth_endpoint_pending(const goth_endpoint_t *endpoint)
{
    return endpoint_storage_ok(endpoint) ? endpoint->count : 0U;
}

goth_status_t goth_endpoint_send(goth_endpoint_t *endpoint,
                                 uint32_t type,
                                 uintptr_t value0,
                                 uintptr_t value1)
{
    size_t tail;
    goth_message_t *message;
    goth_task_t *sender;

    if (!endpoint_storage_ok(endpoint)) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (endpoint->count >= endpoint->capacity) {
        return GOTH_STATUS_FULL;
    }

    tail = endpoint->count >= endpoint->capacity - endpoint->head
               ? endpoint->count - (endpoint->capacity - endpoint->head)
               : endpoint->head + endpoint->count;
    message = &endpoint->storage[tail];
    sender = goth_kernel_current_task();
    message->sender = sender != NULL ? sender->id : 0U;
    message->type = type;
    message->value[0] = value0;
    message->value[1] = value1;
    endpoint->count++;
    return GOTH_STATUS_OK;
}

goth_status_t goth_endpoint_receive(goth_endpoint_t *endpoint,
                                    goth_message_t *message)
{
    if (endpoint == NULL || message == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (!endpoint_storage_ok(endpoint)) {
        return GOTH_STATUS_BAD_STATE;
    }
    if (endpoint->count == 0U) {
        return GOTH_STATUS_EMPTY;
    }

    *message = endpoint->storage[endpoint->head];
    endpoint->head = (endpoint->head + 1U) % endpoint->capacity;
    endpoint->count--;
    return GOTH_STATUS_OK;
}

goth_status_t goth_endpoint_poll(goth_endpoint_t *endpoint,
                                 goth_message_t *message)
{
    /* Endpoints are non-blocking in the nanokernel: poll is an explicit
     * spelling of receive that userland can use to distinguish "no message"
     * from a real error. A future blocking wait belongs in the syscall
     * layer, not in the queue primitive. */
    return goth_endpoint_receive(endpoint, message);
}
