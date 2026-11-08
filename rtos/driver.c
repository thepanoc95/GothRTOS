#include "gothrtos/driver.h"

static bool string_is_valid(const char *string)
{
    return string != NULL && string[0] != '\0';
}

static bool compatible_list_is_valid(const char *const *compatible,
                                     size_t compatible_count)
{
    size_t index;

    if (compatible == NULL || compatible_count == 0U) {
        return false;
    }
    for (index = 0U; index < compatible_count; index++) {
        if (!string_is_valid(compatible[index])) {
            return false;
        }
    }
    return true;
}

static bool resource_type_is_valid(goth_device_resource_type_t type)
{
    return type >= GOTH_DEVICE_RESOURCE_MEMORY &&
           type <= GOTH_DEVICE_RESOURCE_DMA;
}

static bool strings_equal(const char *left, const char *right)
{
    size_t index = 0U;

    while (left[index] != '\0' && left[index] == right[index]) {
        index++;
    }
    return left[index] == right[index];
}

static bool compatible_lists_match(const goth_device_t *device,
                                   const goth_driver_t *driver)
{
    size_t device_index;
    size_t driver_index;

    for (device_index = 0U; device_index < device->compatible_count;
         device_index++) {
        for (driver_index = 0U; driver_index < driver->compatible_count;
             driver_index++) {
            if (strings_equal(device->compatible[device_index],
                              driver->compatible[driver_index])) {
                return true;
            }
        }
    }
    return false;
}

static bool drivers_match_device(const goth_device_t *device,
                                const goth_driver_t *driver)
{
    if (device->bus_type != driver->bus_type) {
        return false;
    }
    if (device->bus_type != NULL && device->bus_type->match != NULL) {
        return device->bus_type->match(device, driver);
    }
    return compatible_lists_match(device, driver);
}

static bool device_is_registered(const goth_driver_registry_t *registry,
                                 const goth_device_t *candidate)
{
    const goth_device_t *device;

    for (device = registry->devices; device != NULL; device = device->next) {
        if (device == candidate) {
            return true;
        }
    }
    return false;
}

static bool driver_is_registered(const goth_driver_registry_t *registry,
                                 const goth_driver_t *candidate)
{
    const goth_driver_t *driver;

    for (driver = registry->drivers; driver != NULL; driver = driver->next) {
        if (driver == candidate) {
            return true;
        }
    }
    return false;
}

static bool device_name_is_used(const goth_driver_registry_t *registry,
                               const goth_device_t *candidate)
{
    const goth_device_t *device;

    for (device = registry->devices; device != NULL; device = device->next) {
        if (device->bus == candidate->bus &&
            strings_equal(device->name, candidate->name)) {
            return true;
        }
    }
    return false;
}

static bool driver_name_is_used(const goth_driver_registry_t *registry,
                               const goth_driver_t *candidate)
{
    const goth_driver_t *driver;

    for (driver = registry->drivers; driver != NULL; driver = driver->next) {
        if (strings_equal(driver->name, candidate->name)) {
            return true;
        }
    }
    return false;
}

static goth_driver_t *matching_driver(goth_driver_registry_t *registry,
                                      goth_device_t *device)
{
    goth_driver_t *driver;

    for (driver = registry->drivers; driver != NULL; driver = driver->next) {
        if (drivers_match_device(device, driver)) {
            return driver;
        }
    }
    return NULL;
}

static goth_status_t bind_device(goth_device_t *device,
                                 goth_driver_t *driver)
{
    goth_status_t status;

    device->driver = driver;
    device->driver_data = NULL;
    status = driver->probe(device);
    if (status != GOTH_STATUS_OK) {
        device->driver = NULL;
        device->driver_data = NULL;
        return status;
    }
    device->state = GOTH_DEVICE_BOUND;
    return GOTH_STATUS_OK;
}

static goth_status_t probe_registered_device(
    goth_driver_registry_t *registry,
    goth_device_t *device)
{
    goth_driver_t *driver;

    if (device->state != GOTH_DEVICE_REGISTERED) {
        return GOTH_STATUS_OK;
    }
    driver = matching_driver(registry, device);
    if (driver == NULL) {
        return GOTH_STATUS_OK;
    }
    return bind_device(device, driver);
}

void goth_driver_registry_init(goth_driver_registry_t *registry)
{
    if (registry == NULL) {
        return;
    }
    registry->devices = NULL;
    registry->drivers = NULL;
    registry->initialized = true;
}

goth_status_t goth_device_init(
    goth_device_t *device,
    const char *name,
    const char *const *compatible,
    size_t compatible_count,
    goth_device_t *bus,
    const goth_device_resource_t *resources,
    size_t resource_count)
{
    size_t index;

    if (device == NULL || !string_is_valid(name) ||
        !compatible_list_is_valid(compatible, compatible_count) ||
        (resources == NULL && resource_count != 0U)) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0U; index < resource_count; index++) {
        if (!resource_type_is_valid(resources[index].type)) {
            return GOTH_STATUS_INVALID_ARGUMENT;
        }
    }

    device->name = name;
    device->compatible = compatible;
    device->compatible_count = compatible_count;
    device->bus = bus;
    device->bus_type = NULL;
    device->resources = resources;
    device->resource_count = resource_count;
    device->state = GOTH_DEVICE_UNREGISTERED;
    device->driver = NULL;
    device->driver_data = NULL;
    device->registry = NULL;
    device->next = NULL;
    return GOTH_STATUS_OK;
}

goth_status_t goth_driver_init(goth_driver_t *driver,
                               const char *name,
                               const char *const *compatible,
                               size_t compatible_count,
                               goth_driver_probe_t probe,
                               goth_driver_remove_t remove)
{
    if (driver == NULL || !string_is_valid(name) ||
        !compatible_list_is_valid(compatible, compatible_count) ||
        probe == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    driver->name = name;
    driver->compatible = compatible;
    driver->compatible_count = compatible_count;
    driver->bus_type = NULL;
    driver->probe = probe;
    driver->remove = remove;
    driver->state = GOTH_DRIVER_UNREGISTERED;
    driver->registry = NULL;
    driver->next = NULL;
    return GOTH_STATUS_OK;
}

goth_status_t goth_device_set_bus_type(goth_device_t *device,
                                       const goth_bus_type_t *bus_type)
{
    if (device == NULL ||
        (bus_type != NULL && !string_is_valid(bus_type->name))) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (device->state != GOTH_DEVICE_UNREGISTERED ||
        device->registry != NULL) {
        return GOTH_STATUS_BAD_STATE;
    }
    device->bus_type = bus_type;
    return GOTH_STATUS_OK;
}

goth_status_t goth_driver_set_bus_type(goth_driver_t *driver,
                                       const goth_bus_type_t *bus_type)
{
    if (driver == NULL ||
        (bus_type != NULL && !string_is_valid(bus_type->name))) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (driver->state != GOTH_DRIVER_UNREGISTERED ||
        driver->registry != NULL) {
        return GOTH_STATUS_BAD_STATE;
    }
    driver->bus_type = bus_type;
    return GOTH_STATUS_OK;
}

goth_status_t goth_device_get_resource(const goth_device_t *device,
                                       goth_device_resource_type_t type,
                                       size_t index,
                                       goth_device_resource_t *resource)
{
    size_t resource_index;
    size_t match_index = 0U;

    if (device == NULL || resource == NULL ||
        !resource_type_is_valid(type)) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    for (resource_index = 0U; resource_index < device->resource_count;
         resource_index++) {
        if (device->resources[resource_index].type == type) {
            if (match_index == index) {
                *resource = device->resources[resource_index];
                return GOTH_STATUS_OK;
            }
            match_index++;
        }
    }
    return GOTH_STATUS_EMPTY;
}

size_t goth_device_child_count(const goth_device_t *device)
{
    const goth_device_t *child;
    size_t count = 0U;

    if (device == NULL || device->registry == NULL ||
        !device_is_registered(device->registry, device)) {
        return 0U;
    }
    for (child = device->registry->devices; child != NULL;
         child = child->next) {
        if (child->bus == device) {
            count++;
        }
    }
    return count;
}

goth_device_t *goth_device_child_at(goth_device_t *device, size_t index)
{
    goth_device_t *child;

    if (device == NULL || device->registry == NULL ||
        !device_is_registered(device->registry, device)) {
        return NULL;
    }
    for (child = device->registry->devices; child != NULL;
         child = child->next) {
        if (child->bus == device) {
            if (index == 0U) {
                return child;
            }
            index--;
        }
    }
    return NULL;
}

goth_status_t goth_driver_registry_probe(goth_driver_registry_t *registry)
{
    goth_device_t *device;
    goth_status_t result = GOTH_STATUS_OK;

    if (registry == NULL || !registry->initialized) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    for (device = registry->devices; device != NULL; device = device->next) {
        goth_status_t status = probe_registered_device(registry, device);

        if (status != GOTH_STATUS_OK && result == GOTH_STATUS_OK) {
            result = status;
        }
    }
    return result;
}

goth_status_t goth_device_register(goth_driver_registry_t *registry,
                                   goth_device_t *device)
{
    goth_device_t **tail;
    goth_status_t status;

    if (registry == NULL || !registry->initialized || device == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (device->state != GOTH_DEVICE_UNREGISTERED ||
        device->registry != NULL || device->next != NULL) {
        return GOTH_STATUS_BAD_STATE;
    }
    if (device->bus != NULL &&
        (device->bus->registry != registry ||
         !device_is_registered(registry, device->bus))) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (device_name_is_used(registry, device)) {
        return GOTH_STATUS_BAD_STATE;
    }

    tail = &registry->devices;
    while (*tail != NULL) {
        tail = &(*tail)->next;
    }
    *tail = device;
    device->registry = registry;
    device->state = GOTH_DEVICE_REGISTERED;

    status = probe_registered_device(registry, device);
    return status;
}

goth_status_t goth_driver_register(goth_driver_registry_t *registry,
                                   goth_driver_t *driver)
{
    goth_driver_t **tail;
    goth_device_t *device;
    goth_status_t result = GOTH_STATUS_OK;
    goth_status_t status;

    if (registry == NULL || !registry->initialized || driver == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (driver->state != GOTH_DRIVER_UNREGISTERED ||
        driver->registry != NULL || driver->next != NULL) {
        return GOTH_STATUS_BAD_STATE;
    }
    if (driver_name_is_used(registry, driver)) {
        return GOTH_STATUS_BAD_STATE;
    }

    tail = &registry->drivers;
    while (*tail != NULL) {
        tail = &(*tail)->next;
    }
    *tail = driver;
    driver->registry = registry;
    driver->state = GOTH_DRIVER_REGISTERED;

    for (device = registry->devices; device != NULL; device = device->next) {
        if (device->state == GOTH_DEVICE_REGISTERED &&
            matching_driver(registry, device) == driver) {
            status = bind_device(device, driver);
            if (status != GOTH_STATUS_OK && result == GOTH_STATUS_OK) {
                result = status;
            }
        }
    }
    return result;
}

goth_status_t goth_device_unbind(goth_device_t *device)
{
    goth_status_t status;

    if (device == NULL || device->registry == NULL ||
        !device_is_registered(device->registry, device)) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (device->state != GOTH_DEVICE_BOUND || device->driver == NULL) {
        return GOTH_STATUS_BAD_STATE;
    }
    if (goth_device_child_count(device) != 0U) {
        return GOTH_STATUS_BAD_STATE;
    }

    if (device->driver->remove != NULL) {
        status = device->driver->remove(device);
        if (status != GOTH_STATUS_OK) {
            return status;
        }
    }
    device->driver = NULL;
    device->driver_data = NULL;
    device->state = GOTH_DEVICE_REGISTERED;
    return GOTH_STATUS_OK;
}

goth_status_t goth_device_unregister(goth_device_t *device)
{
    goth_driver_registry_t *registry;
    goth_device_t **link;
    goth_status_t status;

    if (device == NULL || device->registry == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    registry = device->registry;
    if (!device_is_registered(registry, device)) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    {
        const goth_device_t *child;

        for (child = registry->devices; child != NULL; child = child->next) {
            if (child->bus == device) {
                return GOTH_STATUS_BAD_STATE;
            }
        }
    }
    if (device->state == GOTH_DEVICE_BOUND) {
        status = goth_device_unbind(device);
        if (status != GOTH_STATUS_OK) {
            return status;
        }
    }

    link = &registry->devices;
    while (*link != device) {
        link = &(*link)->next;
    }
    *link = device->next;
    device->next = NULL;
    device->registry = NULL;
    device->state = GOTH_DEVICE_UNREGISTERED;
    return GOTH_STATUS_OK;
}

goth_status_t goth_driver_unregister(goth_driver_t *driver)
{
    goth_driver_registry_t *registry;
    goth_device_t *device;
    goth_driver_t **link;

    if (driver == NULL || driver->registry == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    registry = driver->registry;
    if (!driver_is_registered(registry, driver)) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    for (device = registry->devices; device != NULL; device = device->next) {
        if (device->driver == driver &&
            goth_device_child_count(device) != 0U) {
            return GOTH_STATUS_BAD_STATE;
        }
    }
    for (device = registry->devices; device != NULL; device = device->next) {
        if (device->driver == driver) {
            goth_status_t status = goth_device_unbind(device);

            if (status != GOTH_STATUS_OK) {
                return status;
            }
        }
    }

    link = &registry->drivers;
    while (*link != driver) {
        link = &(*link)->next;
    }
    *link = driver->next;
    driver->next = NULL;
    driver->registry = NULL;
    driver->state = GOTH_DRIVER_UNREGISTERED;
    return GOTH_STATUS_OK;
}

goth_device_t *goth_device_find(goth_driver_registry_t *registry,
                                goth_device_t *bus,
                                const char *name)
{
    goth_device_t *device;

    if (registry == NULL || !registry->initialized ||
        !string_is_valid(name)) {
        return NULL;
    }
    for (device = registry->devices; device != NULL; device = device->next) {
        if (device->bus == bus && strings_equal(device->name, name)) {
            return device;
        }
    }
    return NULL;
}
