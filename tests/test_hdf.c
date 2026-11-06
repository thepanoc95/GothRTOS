#include "gothrtos/hdf.h"
#include "gothrtos/klog.h"
#include "gothrtos/nanokernel.h"

#include <assert.h>
#include <string.h>

/* Exercises the Hardware Driver Foundation in isolation: bus/device/driver
 * registration, matching, probe/start lifecycle, power management and the
 * device enumeration helpers. No platform port is required. */

static int probe_count;
static int start_count;
static int stop_count;
static int suspend_count;
static int resume_count;
static int remove_count;

static goth_hdf_status_t test_probe(goth_hdf_device_t *device)
{
    probe_count++;
    device->driver_data = (void *)(uintptr_t)0x1234U;
    return GOTH_HDF_OK;
}

static goth_hdf_status_t test_remove(goth_hdf_device_t *device)
{
    (void)device;
    remove_count++;
    return GOTH_HDF_OK;
}

static goth_hdf_status_t test_start(goth_hdf_device_t *device)
{
    (void)device;
    start_count++;
    return GOTH_HDF_OK;
}

static goth_hdf_status_t test_stop(goth_hdf_device_t *device)
{
    (void)device;
    stop_count++;
    return GOTH_HDF_OK;
}

static goth_hdf_status_t test_suspend(goth_hdf_device_t *device)
{
    (void)device;
    suspend_count++;
    return GOTH_HDF_OK;
}

static goth_hdf_status_t test_resume(goth_hdf_device_t *device)
{
    (void)device;
    resume_count++;
    return GOTH_HDF_OK;
}

static goth_hdf_status_t fail_probe(goth_hdf_device_t *device)
{
    (void)device;
    return GOTH_HDF_DEVICE_ERROR;
}

static goth_hdf_device_t console_device = {
    "test-uart", "goth,test-uart", GOTH_HDF_BUS_PLATFORM,
    GOTH_HDF_CLASS_CONSOLE, 0U, {{0x1000U, 0x100U, 0U}}, 1U, {{5U, 0U}}, 1U,
    NULL, NULL, GOTH_HDF_DEVICE_UNREGISTERED, GOTH_HDF_POWER_OFF, NULL
};

static goth_hdf_device_t timer_device = {
    "test-timer", "goth,test-timer", GOTH_HDF_BUS_PLATFORM,
    GOTH_HDF_CLASS_TIMER, 0U, {{0U, 0U, 0U}}, 0U, {{0U, 0U}}, 0U,
    NULL, NULL, GOTH_HDF_DEVICE_UNREGISTERED, GOTH_HDF_POWER_OFF, NULL
};

static goth_hdf_device_t orphan_device = {
    "test-orphan", "goth,unknown", GOTH_HDF_BUS_PLATFORM,
    GOTH_HDF_CLASS_MISC, 0U, {{0U, 0U, 0U}}, 0U, {{0U, 0U}}, 0U,
    NULL, NULL, GOTH_HDF_DEVICE_UNREGISTERED, GOTH_HDF_POWER_OFF, NULL
};

static goth_hdf_driver_t console_driver = {
    "test-console", "goth,test-uart", GOTH_HDF_BUS_PLATFORM,
    GOTH_HDF_CLASS_ANY, test_probe, test_remove, test_start, test_stop,
    test_suspend, test_resume, NULL
};

static goth_hdf_driver_t timer_driver = {
    "test-timer-drv", NULL, GOTH_HDF_BUS_PLATFORM, GOTH_HDF_CLASS_TIMER,
    test_probe, test_remove, test_start, test_stop, test_suspend, test_resume,
    NULL
};

static goth_hdf_driver_t failing_driver = {
    "test-failing", "goth,unknown", GOTH_HDF_BUS_PLATFORM,
    GOTH_HDF_CLASS_ANY, fail_probe, NULL, NULL, NULL, NULL, NULL, NULL
};

static void reset_counters(void)
{
    probe_count = 0;
    start_count = 0;
    stop_count = 0;
    suspend_count = 0;
    resume_count = 0;
    remove_count = 0;
}

static void test_registration(void)
{
    goth_hdf_init();
    assert(goth_hdf_is_initialized());
    assert(goth_hdf_bus_count() == 1U);
    assert(strcmp(goth_hdf_bus_at(0)->name, "platform") == 0);

    assert(goth_hdf_register_device(&console_device) == GOTH_HDF_OK);
    assert(goth_hdf_register_device(&console_device) == GOTH_HDF_DUPLICATE);
    assert(goth_hdf_register_device(&timer_device) == GOTH_HDF_OK);
    assert(goth_hdf_device_count() == 2U);

    assert(goth_hdf_register_driver(&console_driver) == GOTH_HDF_OK);
    assert(goth_hdf_register_driver(&console_driver) == GOTH_HDF_DUPLICATE);
    assert(goth_hdf_driver_count() == 1U);

    assert(goth_hdf_device_find("test-uart") == &console_device);
    assert(goth_hdf_device_find("missing") == NULL);
    assert(goth_hdf_driver_find("test-console") == &console_driver);
    assert(goth_hdf_bus_at(0)->device_count == 2U);
}

static void test_matching(void)
{
    assert(goth_hdf_match(&console_device, &console_driver));
    /* Compatible strings differ. */
    assert(!goth_hdf_match(&timer_device, &console_driver));
    /* A class driver with a wildcard compatible matches on class alone. */
    assert(goth_hdf_match(&timer_device, &timer_driver));
    assert(!goth_hdf_match(&console_device, &timer_driver));
    /* Bus mismatch never matches. */
    {
        goth_hdf_driver_t pci_driver = {
            "pci-drv", NULL, GOTH_HDF_BUS_PCI, GOTH_HDF_CLASS_ANY,
            NULL, NULL, NULL, NULL, NULL, NULL, NULL
        };
        assert(!goth_hdf_match(&console_device, &pci_driver));
    }
}

static void test_lifecycle(void)
{
    reset_counters();
    goth_hdf_init();
    assert(goth_hdf_register_device(&console_device) == GOTH_HDF_OK);
    assert(goth_hdf_register_driver(&console_driver) == GOTH_HDF_OK);

    assert(goth_hdf_probe_device(&console_device) == GOTH_HDF_OK);
    assert(console_device.state == GOTH_HDF_DEVICE_STARTED);
    assert(console_device.driver == &console_driver);
    assert(console_device.driver_data == (void *)(uintptr_t)0x1234U);
    assert(probe_count == 1 && start_count == 1);
    assert(goth_hdf_started_count() == 1U);

    /* Probe is idempotent once started. */
    assert(goth_hdf_probe_device(&console_device) == GOTH_HDF_OK);
    assert(probe_count == 1 && start_count == 1);

    assert(goth_hdf_set_power(&console_device, GOTH_HDF_POWER_SUSPEND) ==
           GOTH_HDF_OK);
    assert(console_device.state == GOTH_HDF_DEVICE_SUSPENDED);
    assert(suspend_count == 1);

    assert(goth_hdf_set_power(&console_device, GOTH_HDF_POWER_ON) ==
           GOTH_HDF_OK);
    assert(console_device.state == GOTH_HDF_DEVICE_STARTED);
    assert(resume_count == 1);

    assert(goth_hdf_set_power(&console_device, GOTH_HDF_POWER_OFF) ==
           GOTH_HDF_OK);
    assert(console_device.state == GOTH_HDF_DEVICE_PROBED);
    assert(stop_count == 1);

    assert(goth_hdf_unregister_device(&console_device) == GOTH_HDF_OK);
    assert(console_device.state == GOTH_HDF_DEVICE_REMOVED);
    assert(goth_hdf_device_count() == 0U);
    assert(goth_hdf_unregister_device(&console_device) == GOTH_HDF_NOT_FOUND);
}

static void test_probe_all_and_failure(void)
{
    reset_counters();
    goth_hdf_init();
    assert(goth_hdf_register_device(&console_device) == GOTH_HDF_OK);
    assert(goth_hdf_register_device(&timer_device) == GOTH_HDF_OK);
    assert(goth_hdf_register_device(&orphan_device) == GOTH_HDF_OK);
    assert(goth_hdf_register_driver(&console_driver) == GOTH_HDF_OK);
    assert(goth_hdf_register_driver(&timer_driver) == GOTH_HDF_OK);
    assert(goth_hdf_register_driver(&failing_driver) == GOTH_HDF_OK);

    /* console and timer bind; orphan matches only the failing driver. */
    assert(goth_hdf_probe_all() == 2U);
    assert(goth_hdf_started_count() == 2U);
    assert(orphan_device.state == GOTH_HDF_DEVICE_FAILED);

    /* A device with no matching driver at all is reported, not fatal. */
    goth_hdf_init();
    assert(goth_hdf_register_device(&orphan_device) == GOTH_HDF_OK);
    assert(goth_hdf_probe_all() == 0U);
    assert(goth_hdf_probe_device(&orphan_device) == GOTH_HDF_NO_DRIVER);
}

static void test_suspend_resume_all(void)
{
    reset_counters();
    goth_hdf_init();
    assert(goth_hdf_register_device(&console_device) == GOTH_HDF_OK);
    assert(goth_hdf_register_device(&timer_device) == GOTH_HDF_OK);
    assert(goth_hdf_register_driver(&console_driver) == GOTH_HDF_OK);
    assert(goth_hdf_register_driver(&timer_driver) == GOTH_HDF_OK);
    assert(goth_hdf_probe_all() == 2U);

    assert(goth_hdf_suspend_all() == GOTH_HDF_OK);
    assert(suspend_count == 2);
    assert(console_device.power == GOTH_HDF_POWER_SUSPEND);
    assert(timer_device.power == GOTH_HDF_POWER_SUSPEND);

    assert(goth_hdf_resume_all() == GOTH_HDF_OK);
    assert(resume_count == 2);
    assert(console_device.state == GOTH_HDF_DEVICE_STARTED);
    assert(timer_device.state == GOTH_HDF_DEVICE_STARTED);
}

static void test_enumeration_helpers(void)
{
    assert(strcmp(goth_hdf_bus_name(GOTH_HDF_BUS_I2C), "i2c") == 0);
    assert(strcmp(goth_hdf_class_name(GOTH_HDF_CLASS_BLOCK), "block") == 0);
    assert(strcmp(goth_hdf_state_name(GOTH_HDF_DEVICE_STARTED), "started") ==
           0);
    assert(strcmp(goth_hdf_power_name(GOTH_HDF_POWER_SUSPEND), "suspend") == 0);

    assert(goth_hdf_device_find_class(GOTH_HDF_CLASS_CONSOLE, 0) ==
           &console_device);
    assert(goth_hdf_device_find_class(GOTH_HDF_CLASS_CONSOLE, 1) == NULL);
}

static void test_errors(void)
{
    goth_hdf_init();
    assert(goth_hdf_register_device(NULL) == GOTH_HDF_INVALID_ARGUMENT);
    assert(goth_hdf_register_driver(NULL) == GOTH_HDF_INVALID_ARGUMENT);
    assert(goth_hdf_probe_device(NULL) == GOTH_HDF_INVALID_ARGUMENT);
    assert(goth_hdf_set_power(NULL, GOTH_HDF_POWER_ON) ==
           GOTH_HDF_INVALID_ARGUMENT);
    assert(goth_hdf_unregister_driver(&timer_driver) == GOTH_HDF_NOT_FOUND);
    assert(goth_hdf_unregister_device(&timer_device) == GOTH_HDF_NOT_FOUND);
}

int main(void)
{
    goth_klog_init();
    test_registration();
    test_matching();
    test_lifecycle();
    test_probe_all_and_failure();
    test_suspend_resume_all();
    test_enumeration_helpers();
    test_errors();
    return 0;
}
