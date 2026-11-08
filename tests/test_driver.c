#include "gothrtos/driver.h"

#include <assert.h>
#include <string.h>

static unsigned int probe_count;
static unsigned int remove_count;
static goth_status_t next_probe_status;
static goth_status_t next_remove_status;
static int driver_context;

static goth_status_t probe_device(goth_device_t *device)
{
    probe_count++;
    device->driver_data = &driver_context;
    return next_probe_status;
}

static goth_status_t remove_device(goth_device_t *device)
{
    assert(device->driver_data == &driver_context);
    remove_count++;
    return next_remove_status;
}

static goth_status_t probe_bus(goth_device_t *device)
{
    (void)device;
    return GOTH_STATUS_OK;
}

static goth_status_t remove_bus(goth_device_t *device)
{
    (void)device;
    return GOTH_STATUS_OK;
}

static bool model_bus_match(const goth_device_t *device,
                            const goth_driver_t *driver)
{
    return device->compatible_count > 0U &&
           strcmp(device->compatible[0], "vendor,device-v2") == 0 &&
           strcmp(driver->name, "model-driver") == 0;
}

static void test_registration_and_lifecycle(void)
{
    static const goth_bus_type_t model_bus = {
        "model-bus", model_bus_match};
    static const char *const bus_compatible[] = {"simple-bus"};
    static const char *const device_compatible[] = {
        "vendor,device-v2", "vendor,device-v1"};
    static const char *const driver_compatible[] = {"vendor,device-v1"};
    static const char *const bus_driver_compatible[] = {"simple-bus"};
    static const goth_device_resource_t resources[] = {
        {GOTH_DEVICE_RESOURCE_MEMORY, 0x1000U, 0x100U, 0U},
        {GOTH_DEVICE_RESOURCE_INTERRUPT, 5U, 1U, 0U}};
    goth_driver_registry_t registry;
    goth_device_t bus;
    goth_device_t device;
    goth_device_t untyped_sibling;
    goth_driver_t driver;
    goth_driver_t bus_driver;

    probe_count = 0U;
    remove_count = 0U;
    next_probe_status = GOTH_STATUS_OK;
    next_remove_status = GOTH_STATUS_OK;
    goth_driver_registry_init(&registry);
    assert(goth_device_init(&bus, "soc", bus_compatible, 1U, NULL, NULL,
                            0U) == GOTH_STATUS_OK);
    assert(goth_device_register(&registry, &bus) == GOTH_STATUS_OK);
    assert(goth_device_init(&device, "uart0", device_compatible, 2U, &bus,
                            resources, 2U) == GOTH_STATUS_OK);
    assert(goth_device_set_bus_type(&device, &model_bus) == GOTH_STATUS_OK);
    assert(goth_device_register(&registry, &device) == GOTH_STATUS_OK);
    assert(goth_device_set_bus_type(&device, NULL) == GOTH_STATUS_BAD_STATE);
    assert(device.state == GOTH_DEVICE_REGISTERED);
    assert(goth_device_find(&registry, &bus, "uart0") == &device);
    assert(device.resources == resources && device.resource_count == 2U);
    assert(goth_device_child_count(&bus) == 1U);
    assert(goth_device_child_at(&bus, 0U) == &device);
    assert(goth_device_child_at(&bus, 1U) == NULL);
    assert(goth_driver_init(&bus_driver, "soc", bus_driver_compatible, 1U,
                            probe_bus, remove_bus) == GOTH_STATUS_OK);
    assert(goth_driver_register(&registry, &bus_driver) == GOTH_STATUS_OK);
    assert(bus.state == GOTH_DEVICE_BOUND);
    assert(goth_device_unbind(&bus) == GOTH_STATUS_BAD_STATE);
    assert(goth_driver_unregister(&bus_driver) == GOTH_STATUS_BAD_STATE);
    {
        goth_device_resource_t resource;

        assert(goth_device_get_resource(&device, GOTH_DEVICE_RESOURCE_MEMORY,
                                        0U, &resource) == GOTH_STATUS_OK);
        assert(resource.start == 0x1000U && resource.size == 0x100U);
        assert(goth_device_get_resource(&device,
                                        GOTH_DEVICE_RESOURCE_INTERRUPT,
                                        0U, &resource) == GOTH_STATUS_OK);
        assert(resource.start == 5U);
        assert(goth_device_get_resource(&device,
                                        GOTH_DEVICE_RESOURCE_INTERRUPT,
                                        1U, &resource) == GOTH_STATUS_EMPTY);
        assert(goth_device_get_resource(&device,
                                        (goth_device_resource_type_t)99,
                                        0U, &resource) ==
               GOTH_STATUS_INVALID_ARGUMENT);
    }
        assert(goth_device_init(&untyped_sibling, "uart1", device_compatible, 2U,
                                &bus, NULL, 0U) == GOTH_STATUS_OK);
        assert(goth_device_register(&registry, &untyped_sibling) ==
               GOTH_STATUS_OK);
        assert(goth_device_child_count(&bus) == 2U);
        assert(goth_device_child_at(&bus, 1U) == &untyped_sibling);

        assert(goth_driver_init(&driver, "model-driver", driver_compatible, 1U,
                           probe_device, remove_device) == GOTH_STATUS_OK);
    assert(goth_driver_set_bus_type(&driver, &model_bus) == GOTH_STATUS_OK);
    assert(goth_driver_register(&registry, &driver) == GOTH_STATUS_OK);
    assert(goth_driver_set_bus_type(&driver, NULL) == GOTH_STATUS_BAD_STATE);
    assert(probe_count == 1U);
    assert(device.state == GOTH_DEVICE_BOUND);
    assert(device.driver == &driver);
    assert(device.driver_data == &driver_context);
    assert(untyped_sibling.state == GOTH_DEVICE_REGISTERED);
    assert(untyped_sibling.driver == NULL);
    assert(goth_device_unbind(&device) == GOTH_STATUS_OK);
    assert(device.state == GOTH_DEVICE_REGISTERED);
    assert(device.driver == NULL && device.driver_data == NULL);
    assert(remove_count == 1U);
    assert(goth_driver_registry_probe(&registry) == GOTH_STATUS_OK);
    assert(device.state == GOTH_DEVICE_BOUND);
    assert(probe_count == 2U);
    assert(goth_device_unregister(&bus) == GOTH_STATUS_BAD_STATE);
    assert(goth_device_unregister(&device) == GOTH_STATUS_OK);
    assert(goth_device_child_count(&bus) == 1U);
    assert(goth_device_unregister(&untyped_sibling) == GOTH_STATUS_OK);
    assert(goth_device_child_count(&bus) == 0U);
    assert(goth_device_child_at(&bus, 0U) == NULL);
    assert(remove_count == 2U);
    assert(goth_device_find(&registry, &bus, "uart0") == NULL);
    assert(goth_driver_unregister(&driver) == GOTH_STATUS_OK);
    assert(goth_driver_unregister(&bus_driver) == GOTH_STATUS_OK);
    assert(goth_device_unregister(&bus) == GOTH_STATUS_OK);
}

static void test_validation_and_probe_failures(void)
{
    static const char *const compatible[] = {"vendor,broken"};
    static const char *const unrelated_compatible[] = {"vendor,other"};
    static const goth_device_resource_t invalid_resource[] = {
        {(goth_device_resource_type_t)99, 0U, 0U, 0U}};
    goth_driver_registry_t registry;
    goth_device_t device;
    goth_device_t unrelated;
    goth_driver_t driver;

    probe_count = 0U;
    remove_count = 0U;
    next_remove_status = GOTH_STATUS_OK;
    goth_driver_registry_init(&registry);
    assert(goth_device_init(NULL, "device", compatible, 1U, NULL, NULL, 0U) ==
           GOTH_STATUS_INVALID_ARGUMENT);
    assert(goth_device_init(&device, "", compatible, 1U, NULL, NULL, 0U) ==
           GOTH_STATUS_INVALID_ARGUMENT);
    assert(goth_device_init(&device, "invalid-resource", compatible, 1U,
                            NULL, invalid_resource, 1U) ==
           GOTH_STATUS_INVALID_ARGUMENT);
    assert(goth_driver_init(&driver, "driver", compatible, 1U, NULL, NULL) ==
           GOTH_STATUS_INVALID_ARGUMENT);
    assert(goth_device_init(&device, "broken0", compatible, 1U, NULL, NULL,
                            0U) == GOTH_STATUS_OK);

    next_probe_status = GOTH_STATUS_PLATFORM_ERROR;
    assert(goth_device_register(&registry, &device) == GOTH_STATUS_OK);
    assert(goth_driver_init(&driver, "broken", compatible, 1U, probe_device,
                            remove_device) == GOTH_STATUS_OK);
    assert(goth_driver_register(&registry, &driver) ==
           GOTH_STATUS_PLATFORM_ERROR);
    assert(driver.state == GOTH_DRIVER_REGISTERED);
    assert(device.state == GOTH_DEVICE_REGISTERED);
    assert(device.driver == NULL && device.driver_data == NULL);
    assert(goth_device_init(&unrelated, "other0", unrelated_compatible, 1U,
                            NULL, NULL, 0U) == GOTH_STATUS_OK);
    assert(goth_device_register(&registry, &unrelated) == GOTH_STATUS_OK);
    assert(probe_count == 1U);
    next_probe_status = GOTH_STATUS_OK;
    assert(goth_driver_registry_probe(&registry) == GOTH_STATUS_OK);
    assert(device.state == GOTH_DEVICE_BOUND);
    assert(probe_count == 2U);

    next_remove_status = GOTH_STATUS_PLATFORM_ERROR;
    assert(goth_device_unregister(&device) == GOTH_STATUS_PLATFORM_ERROR);
    assert(device.state == GOTH_DEVICE_BOUND);
    assert(driver.state == GOTH_DRIVER_REGISTERED);
    next_remove_status = GOTH_STATUS_OK;
    assert(goth_driver_unregister(&driver) == GOTH_STATUS_OK);
    assert(goth_device_unregister(&device) == GOTH_STATUS_OK);
    assert(goth_device_unregister(&unrelated) == GOTH_STATUS_OK);
}

int main(void)
{
    test_registration_and_lifecycle();
    test_validation_and_probe_failures();
    return 0;
}
