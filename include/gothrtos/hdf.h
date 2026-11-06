#ifndef GOTHRTOS_HDF_H
#define GOTHRTOS_HDF_H

#include "gothrtos/kernel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Hardware Driver Foundation (HDF).
 *
 * The HDF is the nanokernel's device model. It is deliberately not a Linux
 * device/driver core and not a bus implementation: it is the small, static
 * bookkeeping that lets a platform port describe its hardware and lets driver
 * code attach to it without any of that reaching userland.
 *
 * The model has three parts:
 *
 *   - buses (platform, pci, usb, i2c, spi, uart, virtual) group devices;
 *   - devices are static descriptors: a name, a compatible string, a class,
 *     a unit address, memory-mapped ranges and interrupts;
 *   - drivers declare what they match (compatible string or device class) and
 *     supply probe/start/stop/suspend/resume callbacks.
 *
 * Registration never allocates: devices and drivers are caller-owned static
 * objects linked into fixed-capacity tables. Probing walks the device list,
 * binds the first matching unbound driver and runs its lifecycle callbacks in
 * order (probe -> start). Power management is a per-device state that the HDF
 * tracks and forwards to the driver. */

#define GOTH_HDF_MAX_DEVICES 32U
#define GOTH_HDF_MAX_DRIVERS 32U
#define GOTH_HDF_MAX_BUSES 8U
#define GOTH_HDF_MAX_RESOURCES 4U

typedef enum {
    GOTH_HDF_OK = 0,
    GOTH_HDF_INVALID_ARGUMENT,
    GOTH_HDF_NOT_INITIALIZED,
    GOTH_HDF_NO_MEMORY,
    GOTH_HDF_DUPLICATE,
    GOTH_HDF_NOT_FOUND,
    GOTH_HDF_NO_DRIVER,
    GOTH_HDF_DEVICE_ERROR,
    GOTH_HDF_BAD_STATE
} goth_hdf_status_t;

typedef enum {
    GOTH_HDF_BUS_PLATFORM = 0,
    GOTH_HDF_BUS_PCI,
    GOTH_HDF_BUS_USB,
    GOTH_HDF_BUS_I2C,
    GOTH_HDF_BUS_SPI,
    GOTH_HDF_BUS_UART,
    GOTH_HDF_BUS_VIRTUAL,
    GOTH_HDF_BUS_ANY
} goth_hdf_bus_t;

typedef enum {
    GOTH_HDF_CLASS_NONE = 0,
    GOTH_HDF_CLASS_CONSOLE,
    GOTH_HDF_CLASS_TIMER,
    GOTH_HDF_CLASS_BLOCK,
    GOTH_HDF_CLASS_NET,
    GOTH_HDF_CLASS_GPIO,
    GOTH_HDF_CLASS_RTC,
    GOTH_HDF_CLASS_FRAMEBUFFER,
    GOTH_HDF_CLASS_MISC,
    GOTH_HDF_CLASS_ANY
} goth_hdf_class_t;

typedef enum {
    GOTH_HDF_DEVICE_UNREGISTERED = 0,
    GOTH_HDF_DEVICE_REGISTERED,
    GOTH_HDF_DEVICE_PROBED,
    GOTH_HDF_DEVICE_STARTED,
    GOTH_HDF_DEVICE_SUSPENDED,
    GOTH_HDF_DEVICE_FAILED,
    GOTH_HDF_DEVICE_REMOVED
} goth_hdf_device_state_t;

typedef enum {
    GOTH_HDF_POWER_OFF = 0,
    GOTH_HDF_POWER_ON,
    GOTH_HDF_POWER_SUSPEND
} goth_hdf_power_t;

typedef struct {
    uint64_t base;
    uint64_t size;
    uint32_t flags;
} goth_hdf_mmio_t;

typedef struct {
    uint32_t number;
    uint32_t flags;
} goth_hdf_irq_t;

struct goth_hdf_driver;

typedef struct goth_hdf_device {
    const char *name;
    const char *compatible;
    goth_hdf_bus_t bus;
    goth_hdf_class_t class_id;
    uint32_t unit;
    goth_hdf_mmio_t mmio[GOTH_HDF_MAX_RESOURCES];
    size_t mmio_count;
    goth_hdf_irq_t irq[GOTH_HDF_MAX_RESOURCES];
    size_t irq_count;
    void *driver_data;
    struct goth_hdf_driver *driver;
    goth_hdf_device_state_t state;
    goth_hdf_power_t power;
    struct goth_hdf_device *next;
} goth_hdf_device_t;

typedef struct goth_hdf_driver {
    const char *name;
    /* NULL matches any compatible string; GOTH_HDF_CLASS_ANY matches any
     * class. A driver matches when bus, compatible and class all match. */
    const char *compatible;
    goth_hdf_bus_t bus;
    goth_hdf_class_t class_id;
    goth_hdf_status_t (*probe)(goth_hdf_device_t *device);
    goth_hdf_status_t (*remove)(goth_hdf_device_t *device);
    goth_hdf_status_t (*start)(goth_hdf_device_t *device);
    goth_hdf_status_t (*stop)(goth_hdf_device_t *device);
    goth_hdf_status_t (*suspend)(goth_hdf_device_t *device);
    goth_hdf_status_t (*resume)(goth_hdf_device_t *device);
    struct goth_hdf_driver *next;
} goth_hdf_driver_t;

typedef struct {
    goth_hdf_bus_t bus;
    const char *name;
    size_t device_count;
} goth_hdf_bus_info_t;

void goth_hdf_init(void);
bool goth_hdf_is_initialized(void);
void goth_hdf_reset(void);

goth_hdf_status_t goth_hdf_register_bus(goth_hdf_bus_t bus, const char *name);
size_t goth_hdf_bus_count(void);
const goth_hdf_bus_info_t *goth_hdf_bus_at(size_t index);

goth_hdf_status_t goth_hdf_register_device(goth_hdf_device_t *device);
goth_hdf_status_t goth_hdf_unregister_device(goth_hdf_device_t *device);
goth_hdf_status_t goth_hdf_register_driver(goth_hdf_driver_t *driver);
goth_hdf_status_t goth_hdf_unregister_driver(goth_hdf_driver_t *driver);

/* True when the driver declares it can handle the device. */
bool goth_hdf_match(const goth_hdf_device_t *device,
                    const goth_hdf_driver_t *driver);

/* Binds drivers to devices and runs probe/start. Returns the number of
 * devices that reached the STARTED state during this call. */
size_t goth_hdf_probe_all(void);
goth_hdf_status_t goth_hdf_probe_device(goth_hdf_device_t *device);

goth_hdf_status_t goth_hdf_suspend_all(void);
goth_hdf_status_t goth_hdf_resume_all(void);
goth_hdf_status_t goth_hdf_set_power(goth_hdf_device_t *device,
                                     goth_hdf_power_t power);

size_t goth_hdf_device_count(void);
size_t goth_hdf_driver_count(void);
size_t goth_hdf_started_count(void);
goth_hdf_device_t *goth_hdf_device_at(size_t index);
goth_hdf_driver_t *goth_hdf_driver_at(size_t index);
goth_hdf_device_t *goth_hdf_device_find(const char *name);
goth_hdf_device_t *goth_hdf_device_find_class(goth_hdf_class_t class_id,
                                              size_t index);
goth_hdf_driver_t *goth_hdf_driver_find(const char *name);

const char *goth_hdf_bus_name(goth_hdf_bus_t bus);
const char *goth_hdf_class_name(goth_hdf_class_t class_id);
const char *goth_hdf_state_name(goth_hdf_device_state_t state);
const char *goth_hdf_power_name(goth_hdf_power_t power);

/* Publishes the HDF device and driver tables to the kernel log so a boot
 * transcript records the discovered hardware. */
void goth_hdf_dump(void);

#endif
