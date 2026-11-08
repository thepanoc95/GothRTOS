#include "gothrtos/hdf.h"

#include "gothrtos/klog.h"

/* Local freestanding string helpers: the HDF lives in the nanokernel and must
 * stay usable on bare metal, where no C library is linked. */
static size_t hdf_strlen(const char *text)
{
    size_t length = 0U;

    if (text == NULL) {
        return 0U;
    }
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static bool hdf_equals(const char *first, const char *second)
{
    if (first == NULL || second == NULL) {
        return first == second;
    }
    while (*first != '\0' && *first == *second) {
        first++;
        second++;
    }
    return *first == *second;
}

static bool hdf_compatible_matches(const char *device, const char *driver)
{
    /* A driver with no compatible string is a wildcard. Device-tree style
     * "a\0b\0\0" lists are matched entry by entry. */
    if (driver == NULL) {
        return true;
    }
    if (device == NULL) {
        return false;
    }

    {
        const char *entry = device;

        while (*entry != '\0') {
            if (hdf_equals(entry, driver)) {
                return true;
            }
            entry += hdf_strlen(entry) + 1U;
        }
    }
    return hdf_equals(device, driver);
}

static goth_hdf_device_t *device_table[GOTH_HDF_MAX_DEVICES];
static size_t device_count;
static goth_hdf_driver_t *driver_table[GOTH_HDF_MAX_DRIVERS];
static size_t driver_count;

typedef struct {
    goth_hdf_bus_t bus;
    const char *name;
    bool used;
} hdf_bus_slot_t;

static hdf_bus_slot_t bus_table[GOTH_HDF_MAX_BUSES];
static size_t bus_count;
static bool hdf_initialized;

void goth_hdf_init(void)
{
    size_t index;

    for (index = 0U; index < GOTH_HDF_MAX_DEVICES; index++) {
        device_table[index] = NULL;
    }
    for (index = 0U; index < GOTH_HDF_MAX_DRIVERS; index++) {
        driver_table[index] = NULL;
    }
    for (index = 0U; index < GOTH_HDF_MAX_BUSES; index++) {
        bus_table[index].bus = GOTH_HDF_BUS_ANY;
        bus_table[index].name = NULL;
        bus_table[index].used = false;
    }
    device_count = 0U;
    driver_count = 0U;
    bus_count = 0U;
    hdf_initialized = true;

    /* The platform bus always exists; every board port hangs its devices off
     * it. Other buses are registered by the port or by a driver. */
    (void)goth_hdf_register_bus(GOTH_HDF_BUS_PLATFORM, "platform");
}

bool goth_hdf_is_initialized(void)
{
    return hdf_initialized;
}

void goth_hdf_reset(void)
{
    size_t index;

    for (index = 0U; index < device_count; index++) {
        goth_hdf_device_t *device = device_table[index];

        if (device != NULL) {
            device->driver = NULL;
            device->driver_data = NULL;
            device->state = GOTH_HDF_DEVICE_UNREGISTERED;
            device->power = GOTH_HDF_POWER_OFF;
            device->next = NULL;
        }
        device_table[index] = NULL;
    }
    for (index = 0U; index < driver_count; index++) {
        if (driver_table[index] != NULL) {
            driver_table[index]->next = NULL;
        }
        driver_table[index] = NULL;
    }
    device_count = 0U;
    driver_count = 0U;
    hdf_initialized = false;
}

goth_hdf_status_t goth_hdf_register_bus(goth_hdf_bus_t bus, const char *name)
{
    size_t index;

    if (name == NULL) {
        return GOTH_HDF_INVALID_ARGUMENT;
    }
    for (index = 0U; index < bus_count; index++) {
        if (bus_table[index].bus == bus) {
            return GOTH_HDF_DUPLICATE;
        }
    }
    if (bus_count >= GOTH_HDF_MAX_BUSES) {
        return GOTH_HDF_NO_MEMORY;
    }

    bus_table[bus_count].bus = bus;
    bus_table[bus_count].name = name;
    bus_table[bus_count].used = true;
    bus_count++;
    return GOTH_HDF_OK;
}

size_t goth_hdf_bus_count(void)
{
    return bus_count;
}

const goth_hdf_bus_info_t *goth_hdf_bus_at(size_t index)
{
    static goth_hdf_bus_info_t info;

    if (index >= bus_count) {
        return NULL;
    }
    info.bus = bus_table[index].bus;
    info.name = bus_table[index].name;
    info.device_count = 0U;
    {
        size_t device_index;

        for (device_index = 0U; device_index < device_count; device_index++) {
            if (device_table[device_index] != NULL &&
                device_table[device_index]->bus == bus_table[index].bus) {
                info.device_count++;
            }
        }
    }
    return &info;
}

static bool device_is_registered(const goth_hdf_device_t *device)
{
    size_t index;

    for (index = 0U; index < device_count; index++) {
        if (device_table[index] == device) {
            return true;
        }
    }
    return false;
}

static bool driver_is_registered(const goth_hdf_driver_t *driver)
{
    size_t index;

    for (index = 0U; index < driver_count; index++) {
        if (driver_table[index] == driver) {
            return true;
        }
    }
    return false;
}

goth_hdf_status_t goth_hdf_register_device(goth_hdf_device_t *device)
{
    if (!hdf_initialized) {
        return GOTH_HDF_NOT_INITIALIZED;
    }
    if (device == NULL || device->name == NULL) {
        return GOTH_HDF_INVALID_ARGUMENT;
    }
    if (device_is_registered(device)) {
        return GOTH_HDF_DUPLICATE;
    }
    if (device_count >= GOTH_HDF_MAX_DEVICES) {
        return GOTH_HDF_NO_MEMORY;
    }

    device->driver = NULL;
    device->driver_data = NULL;
    device->power = GOTH_HDF_POWER_OFF;
    device->state = GOTH_HDF_DEVICE_REGISTERED;
    device->next = NULL;
    device_table[device_count++] = device;
    return GOTH_HDF_OK;
}

goth_hdf_status_t goth_hdf_unregister_device(goth_hdf_device_t *device)
{
    size_t index;

    if (!hdf_initialized || device == NULL) {
        return GOTH_HDF_INVALID_ARGUMENT;
    }
    for (index = 0U; index < device_count; index++) {
        if (device_table[index] == device) {
            device_table[index] = device_table[device_count - 1U];
            device_table[--device_count] = NULL;
            device->state = GOTH_HDF_DEVICE_REMOVED;
            device->driver = NULL;
            return GOTH_HDF_OK;
        }
    }
    return GOTH_HDF_NOT_FOUND;
}

goth_hdf_status_t goth_hdf_register_driver(goth_hdf_driver_t *driver)
{
    if (!hdf_initialized) {
        return GOTH_HDF_NOT_INITIALIZED;
    }
    if (driver == NULL || driver->name == NULL) {
        return GOTH_HDF_INVALID_ARGUMENT;
    }
    if (driver_is_registered(driver)) {
        return GOTH_HDF_DUPLICATE;
    }
    if (driver_count >= GOTH_HDF_MAX_DRIVERS) {
        return GOTH_HDF_NO_MEMORY;
    }

    driver->next = NULL;
    driver_table[driver_count++] = driver;
    return GOTH_HDF_OK;
}

goth_hdf_status_t goth_hdf_unregister_driver(goth_hdf_driver_t *driver)
{
    size_t index;

    if (!hdf_initialized || driver == NULL) {
        return GOTH_HDF_INVALID_ARGUMENT;
    }
    for (index = 0U; index < driver_count; index++) {
        if (driver_table[index] == driver) {
            driver_table[index] = driver_table[driver_count - 1U];
            driver_table[--driver_count] = NULL;
            return GOTH_HDF_OK;
        }
    }
    return GOTH_HDF_NOT_FOUND;
}

bool goth_hdf_match(const goth_hdf_device_t *device,
                    const goth_hdf_driver_t *driver)
{
    if (device == NULL || driver == NULL) {
        return false;
    }
    if (driver->bus != GOTH_HDF_BUS_ANY && driver->bus != device->bus) {
        return false;
    }
    if (driver->class_id != GOTH_HDF_CLASS_ANY &&
        driver->class_id != device->class_id) {
        return false;
    }
    return hdf_compatible_matches(device->compatible, driver->compatible);
}

goth_hdf_status_t goth_hdf_probe_device(goth_hdf_device_t *device)
{
    size_t index;

    if (!hdf_initialized) {
        return GOTH_HDF_NOT_INITIALIZED;
    }
    if (device == NULL || !device_is_registered(device)) {
        return GOTH_HDF_INVALID_ARGUMENT;
    }
    if (device->state == GOTH_HDF_DEVICE_STARTED) {
        return GOTH_HDF_OK;
    }

    for (index = 0U; index < driver_count; index++) {
        goth_hdf_driver_t *driver = driver_table[index];
        goth_hdf_status_t status;

        if (driver == NULL || !goth_hdf_match(device, driver)) {
            continue;
        }
        if (driver->probe != NULL) {
            status = driver->probe(device);
            if (status != GOTH_HDF_OK) {
                device->state = GOTH_HDF_DEVICE_FAILED;
                goth_klog_printf(GOTH_LOG_ERR,
                                 "hdf: %s probe failed (%d)",
                                 device->name, (int)status);
                return status;
            }
        }
        device->driver = driver;
        device->state = GOTH_HDF_DEVICE_PROBED;

        if (driver->start != NULL) {
            status = driver->start(device);
            if (status != GOTH_HDF_OK) {
                device->state = GOTH_HDF_DEVICE_FAILED;
                goth_klog_printf(GOTH_LOG_ERR,
                                 "hdf: %s start failed (%d)",
                                 device->name, (int)status);
                return status;
            }
        }
        device->state = GOTH_HDF_DEVICE_STARTED;
        device->power = GOTH_HDF_POWER_ON;
        goth_klog_printf(GOTH_LOG_INFO, "hdf: %s bound to %s",
                         device->name, driver->name);
        return GOTH_HDF_OK;
    }

    device->state = GOTH_HDF_DEVICE_REGISTERED;
    goth_klog_printf(GOTH_LOG_WARNING, "hdf: no driver for %s",
                     device->name);
    return GOTH_HDF_NO_DRIVER;
}

size_t goth_hdf_probe_all(void)
{
    size_t index;
    size_t started = 0U;

    for (index = 0U; index < device_count; index++) {
        goth_hdf_device_t *device = device_table[index];

        if (device == NULL) {
            continue;
        }
        if (goth_hdf_probe_device(device) == GOTH_HDF_OK) {
            started++;
        }
    }
    return started;
}

goth_hdf_status_t goth_hdf_suspend_all(void)
{
    size_t index;
    goth_hdf_status_t result = GOTH_HDF_OK;

    for (index = 0U; index < device_count; index++) {
        goth_hdf_device_t *device = device_table[index];

        if (device == NULL || device->state != GOTH_HDF_DEVICE_STARTED) {
            continue;
        }
        if (goth_hdf_set_power(device, GOTH_HDF_POWER_SUSPEND) != GOTH_HDF_OK) {
            result = GOTH_HDF_DEVICE_ERROR;
        }
    }
    return result;
}

goth_hdf_status_t goth_hdf_resume_all(void)
{
    size_t index;
    goth_hdf_status_t result = GOTH_HDF_OK;

    for (index = 0U; index < device_count; index++) {
        goth_hdf_device_t *device = device_table[index];

        if (device == NULL || device->state != GOTH_HDF_DEVICE_SUSPENDED) {
            continue;
        }
        if (goth_hdf_set_power(device, GOTH_HDF_POWER_ON) != GOTH_HDF_OK) {
            result = GOTH_HDF_DEVICE_ERROR;
        }
    }
    return result;
}

goth_hdf_status_t goth_hdf_set_power(goth_hdf_device_t *device,
                                     goth_hdf_power_t power)
{
    goth_hdf_driver_t *driver;

    if (device == NULL || !device_is_registered(device)) {
        return GOTH_HDF_INVALID_ARGUMENT;
    }
    if (device->power == power) {
        return GOTH_HDF_OK;
    }
    driver = device->driver;

    switch (power) {
    case GOTH_HDF_POWER_SUSPEND:
        if (device->state != GOTH_HDF_DEVICE_STARTED) {
            return GOTH_HDF_BAD_STATE;
        }
        if (driver != NULL && driver->suspend != NULL &&
            driver->suspend(device) != GOTH_HDF_OK) {
            return GOTH_HDF_DEVICE_ERROR;
        }
        device->state = GOTH_HDF_DEVICE_SUSPENDED;
        break;
    case GOTH_HDF_POWER_ON:
        if (device->state == GOTH_HDF_DEVICE_SUSPENDED) {
            if (driver != NULL && driver->resume != NULL &&
                driver->resume(device) != GOTH_HDF_OK) {
                return GOTH_HDF_DEVICE_ERROR;
            }
            device->state = GOTH_HDF_DEVICE_STARTED;
        }
        break;
    case GOTH_HDF_POWER_OFF:
    default:
        if (device->state == GOTH_HDF_DEVICE_STARTED ||
            device->state == GOTH_HDF_DEVICE_SUSPENDED) {
            if (driver != NULL && driver->stop != NULL) {
                (void)driver->stop(device);
            }
            device->state = GOTH_HDF_DEVICE_PROBED;
        }
        break;
    }

    device->power = power;
    return GOTH_HDF_OK;
}

size_t goth_hdf_device_count(void)
{
    return device_count;
}

size_t goth_hdf_driver_count(void)
{
    return driver_count;
}

size_t goth_hdf_started_count(void)
{
    size_t index;
    size_t count = 0U;

    for (index = 0U; index < device_count; index++) {
        if (device_table[index] != NULL &&
            device_table[index]->state == GOTH_HDF_DEVICE_STARTED) {
            count++;
        }
    }
    return count;
}

goth_hdf_device_t *goth_hdf_device_at(size_t index)
{
    if (index >= device_count) {
        return NULL;
    }
    return device_table[index];
}

goth_hdf_driver_t *goth_hdf_driver_at(size_t index)
{
    if (index >= driver_count) {
        return NULL;
    }
    return driver_table[index];
}

goth_hdf_device_t *goth_hdf_device_find(const char *name)
{
    size_t index;

    if (name == NULL) {
        return NULL;
    }
    for (index = 0U; index < device_count; index++) {
        if (device_table[index] != NULL &&
            hdf_equals(device_table[index]->name, name)) {
            return device_table[index];
        }
    }
    return NULL;
}

goth_hdf_device_t *goth_hdf_device_find_class(goth_hdf_class_t class_id,
                                              size_t index)
{
    size_t current;
    size_t seen = 0U;

    for (current = 0U; current < device_count; current++) {
        goth_hdf_device_t *device = device_table[current];

        if (device == NULL || device->class_id != class_id) {
            continue;
        }
        if (seen == index) {
            return device;
        }
        seen++;
    }
    return NULL;
}

goth_hdf_driver_t *goth_hdf_driver_find(const char *name)
{
    size_t index;

    if (name == NULL) {
        return NULL;
    }
    for (index = 0U; index < driver_count; index++) {
        if (driver_table[index] != NULL &&
            hdf_equals(driver_table[index]->name, name)) {
            return driver_table[index];
        }
    }
    return NULL;
}

const char *goth_hdf_bus_name(goth_hdf_bus_t bus)
{
    switch (bus) {
    case GOTH_HDF_BUS_PLATFORM:
        return "platform";
    case GOTH_HDF_BUS_PCI:
        return "pci";
    case GOTH_HDF_BUS_USB:
        return "usb";
    case GOTH_HDF_BUS_I2C:
        return "i2c";
    case GOTH_HDF_BUS_SPI:
        return "spi";
    case GOTH_HDF_BUS_UART:
        return "uart";
    case GOTH_HDF_BUS_VIRTUAL:
        return "virtual";
    case GOTH_HDF_BUS_ANY:
        return "any";
    default:
        return "unknown";
    }
}

const char *goth_hdf_class_name(goth_hdf_class_t class_id)
{
    switch (class_id) {
    case GOTH_HDF_CLASS_NONE:
        return "none";
    case GOTH_HDF_CLASS_CONSOLE:
        return "console";
    case GOTH_HDF_CLASS_TIMER:
        return "timer";
    case GOTH_HDF_CLASS_BLOCK:
        return "block";
    case GOTH_HDF_CLASS_NET:
        return "net";
    case GOTH_HDF_CLASS_GPIO:
        return "gpio";
    case GOTH_HDF_CLASS_RTC:
        return "rtc";
    case GOTH_HDF_CLASS_FRAMEBUFFER:
        return "framebuffer";
    case GOTH_HDF_CLASS_MISC:
        return "misc";
    case GOTH_HDF_CLASS_ANY:
        return "any";
    default:
        return "unknown";
    }
}

const char *goth_hdf_state_name(goth_hdf_device_state_t state)
{
    switch (state) {
    case GOTH_HDF_DEVICE_UNREGISTERED:
        return "unregistered";
    case GOTH_HDF_DEVICE_REGISTERED:
        return "registered";
    case GOTH_HDF_DEVICE_PROBED:
        return "probed";
    case GOTH_HDF_DEVICE_STARTED:
        return "started";
    case GOTH_HDF_DEVICE_SUSPENDED:
        return "suspended";
    case GOTH_HDF_DEVICE_FAILED:
        return "failed";
    case GOTH_HDF_DEVICE_REMOVED:
        return "removed";
    default:
        return "unknown";
    }
}

const char *goth_hdf_power_name(goth_hdf_power_t power)
{
    switch (power) {
    case GOTH_HDF_POWER_OFF:
        return "off";
    case GOTH_HDF_POWER_ON:
        return "on";
    case GOTH_HDF_POWER_SUSPEND:
        return "suspend";
    default:
        return "unknown";
    }
}

void goth_hdf_dump(void)
{
    size_t index;

    goth_klog_printf(GOTH_LOG_INFO, "hdf: %zu bus(es), %zu device(s), "
                     "%zu driver(s), %zu started",
                     goth_hdf_bus_count(), goth_hdf_device_count(),
                     goth_hdf_driver_count(), goth_hdf_started_count());
    for (index = 0U; index < device_count; index++) {
        const goth_hdf_device_t *device = device_table[index];

        if (device == NULL) {
            continue;
        }
        goth_klog_printf(GOTH_LOG_INFO,
                         "hdf: %s [%s/%s] %s%s%s",
                         device->name,
                         goth_hdf_bus_name(device->bus),
                         goth_hdf_class_name(device->class_id),
                         goth_hdf_state_name(device->state),
                         device->driver != NULL ? " -> " : "",
                         device->driver != NULL ? device->driver->name : "");
    }
}
