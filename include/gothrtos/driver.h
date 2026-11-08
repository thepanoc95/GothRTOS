#ifndef GOTHRTOS_DRIVER_H
#define GOTHRTOS_DRIVER_H

#include "gothrtos/kernel.h"

typedef struct goth_device goth_device_t;
typedef struct goth_driver goth_driver_t;
typedef struct goth_driver_registry goth_driver_registry_t;
typedef struct goth_bus_type goth_bus_type_t;

typedef enum {
    GOTH_DEVICE_RESOURCE_MEMORY = 0,
    GOTH_DEVICE_RESOURCE_IO,
    GOTH_DEVICE_RESOURCE_INTERRUPT,
    GOTH_DEVICE_RESOURCE_DMA
} goth_device_resource_type_t;

typedef struct {
    goth_device_resource_type_t type;
    uintptr_t start;
    size_t size;
    uint32_t flags;
} goth_device_resource_t;

typedef enum {
    GOTH_DEVICE_UNREGISTERED = 0,
    GOTH_DEVICE_REGISTERED,
    GOTH_DEVICE_BOUND
} goth_device_state_t;

typedef enum {
    GOTH_DRIVER_UNREGISTERED = 0,
    GOTH_DRIVER_REGISTERED
} goth_driver_state_t;

typedef goth_status_t (*goth_driver_probe_t)(goth_device_t *device);
typedef goth_status_t (*goth_driver_remove_t)(goth_device_t *device);
/* A non-NULL bus type with no match callback uses compatible matching. */
typedef bool (*goth_bus_match_t)(const goth_device_t *device,
                                 const goth_driver_t *driver);

struct goth_bus_type {
    const char *name;
    goth_bus_match_t match;
};

struct goth_device {
    const char *name;
    const char *const *compatible;
    size_t compatible_count;
    goth_device_t *bus;
    const goth_bus_type_t *bus_type;
    const goth_device_resource_t *resources;
    size_t resource_count;
    goth_device_state_t state;
    goth_driver_t *driver;
    void *driver_data;
    goth_driver_registry_t *registry;
    goth_device_t *next;
};

struct goth_driver {
    const char *name;
    const char *const *compatible;
    size_t compatible_count;
    const goth_bus_type_t *bus_type;
    goth_driver_probe_t probe;
    goth_driver_remove_t remove;
    goth_driver_state_t state;
    goth_driver_registry_t *registry;
    goth_driver_t *next;
};

struct goth_driver_registry {
    goth_device_t *devices;
    goth_driver_t *drivers;
    bool initialized;
};

void goth_driver_registry_init(goth_driver_registry_t *registry);
goth_status_t goth_device_init(
    goth_device_t *device,
    const char *name,
    const char *const *compatible,
    size_t compatible_count,
    goth_device_t *bus,
    const goth_device_resource_t *resources,
    size_t resource_count);
goth_status_t goth_driver_init(goth_driver_t *driver,
                               const char *name,
                               const char *const *compatible,
                               size_t compatible_count,
                               goth_driver_probe_t probe,
                               goth_driver_remove_t remove);
goth_status_t goth_device_set_bus_type(goth_device_t *device,
                                       const goth_bus_type_t *bus_type);
goth_status_t goth_driver_set_bus_type(goth_driver_t *driver,
                                       const goth_bus_type_t *bus_type);
/* index selects the index-th resource of type, not the index-th overall. */
goth_status_t goth_device_get_resource(const goth_device_t *device,
                                       goth_device_resource_type_t type,
                                       size_t index,
                                       goth_device_resource_t *resource);
size_t goth_device_child_count(const goth_device_t *device);
goth_device_t *goth_device_child_at(goth_device_t *device, size_t index);
goth_status_t goth_device_register(goth_driver_registry_t *registry,
                                   goth_device_t *device);
goth_status_t goth_driver_register(goth_driver_registry_t *registry,
                                   goth_driver_t *driver);
goth_status_t goth_driver_registry_probe(goth_driver_registry_t *registry);
goth_status_t goth_device_unbind(goth_device_t *device);
goth_status_t goth_device_unregister(goth_device_t *device);
goth_status_t goth_driver_unregister(goth_driver_t *driver);
goth_device_t *goth_device_find(goth_driver_registry_t *registry,
                                goth_device_t *bus,
                                const char *name);

#endif
