#include "HardDriverFoundation.h"

#include <cstring>

namespace {

struct DeviceSlot {
    HdfDevice device;
    int driver_index;
};

HdfDevice devices[HDF_MAX_DEVICES]{};
HdfDriver drivers[HDF_MAX_DRIVERS]{};
DeviceSlot device_slots[HDF_MAX_DEVICES]{};
unsigned int device_count = 0;
unsigned int driver_count = 0;

bool valid_name(const char *name) {
    return name != nullptr && name[0] != '\0';
}

}

extern "C" void HdfInitialize(void) {
    device_count = 0;
    driver_count = 0;
    for (unsigned int index = 0; index < HDF_MAX_DEVICES; ++index) {
        devices[index] = {};
        device_slots[index] = {};
        device_slots[index].driver_index = -1;
    }
    for (unsigned int index = 0; index < HDF_MAX_DRIVERS; ++index) {
        drivers[index] = {};
    }
}

extern "C" HdfStatus HdfRegisterDevice(const HdfDevice *device) {
    if (device == nullptr || !valid_name(device->name) ||
        !valid_name(device->compatible)) {
        return HDF_INVALID_ARGUMENT;
    }
    if (device_count == HDF_MAX_DEVICES) {
        return HDF_CAPACITY_EXCEEDED;
    }
    for (unsigned int index = 0; index < device_count; ++index) {
        if (std::strcmp(devices[index].name, device->name) == 0) {
            return HDF_INVALID_ARGUMENT;
        }
    }

    devices[device_count] = *device;
    device_slots[device_count].device = *device;
    device_slots[device_count].driver_index = -1;
    ++device_count;
    return HDF_SUCCESS;
}

extern "C" HdfStatus HdfRegisterDriver(const HdfDriver *driver) {
    if (driver == nullptr || !valid_name(driver->name) ||
        !valid_name(driver->compatible) || driver->start == nullptr) {
        return HDF_INVALID_ARGUMENT;
    }
    if (driver_count == HDF_MAX_DRIVERS) {
        return HDF_CAPACITY_EXCEEDED;
    }
    for (unsigned int index = 0; index < driver_count; ++index) {
        if (std::strcmp(drivers[index].name, driver->name) == 0) {
            return HDF_INVALID_ARGUMENT;
        }
    }

    drivers[driver_count++] = *driver;
    return HDF_SUCCESS;
}

extern "C" HdfStatus HdfStartDrivers(void) {
    HdfStatus result = HDF_SUCCESS;

    for (unsigned int device_index = 0; device_index < device_count; ++device_index) {
        DeviceSlot &slot = device_slots[device_index];
        if (slot.driver_index >= 0) {
            continue;
        }

        bool matched = false;
        bool start_failed = false;
        for (unsigned int driver_index = 0; driver_index < driver_count; ++driver_index) {
            HdfDriver &driver = drivers[driver_index];
            if (std::strcmp(driver.compatible, slot.device.compatible) != 0 ||
                (driver.probe != nullptr && !driver.probe(&slot.device))) {
                continue;
            }

            matched = true;
            if (driver.start(&slot.device) == HDF_SUCCESS) {
                slot.driver_index = static_cast<int>(driver_index);
                break;
            }
            start_failed = true;
        }

        if (slot.driver_index < 0) {
            if (start_failed) {
                result = HDF_START_FAILED;
            } else if (!matched && result == HDF_SUCCESS) {
                result = HDF_NOT_FOUND;
            }
        }
    }

    return result;
}

extern "C" void HdfStopDrivers(void) {
    for (unsigned int device_index = device_count; device_index > 0; --device_index) {
        DeviceSlot &slot = device_slots[device_index - 1];
        if (slot.driver_index < 0) {
            continue;
        }

        HdfDriver &driver = drivers[slot.driver_index];
        if (driver.stop != nullptr) {
            driver.stop(&slot.device);
        }
        slot.driver_index = -1;
    }
}