#ifndef GOTHRTOS_HARD_DRIVER_FOUNDATION_H
#define GOTHRTOS_HARD_DRIVER_FOUNDATION_H

#ifdef __cplusplus
extern "C" {
#endif

#define HDF_MAX_DEVICES 8
#define HDF_MAX_DRIVERS 8

typedef enum {
    HDF_SUCCESS,
    HDF_INVALID_ARGUMENT,
    HDF_CAPACITY_EXCEEDED,
    HDF_NOT_FOUND,
    HDF_START_FAILED
} HdfStatus;

typedef struct {
    const char *name;
    const char *compatible;
    void *context;
} HdfDevice;

typedef int (*HdfProbe)(const HdfDevice *device);
typedef HdfStatus (*HdfStart)(HdfDevice *device);
typedef void (*HdfStop)(HdfDevice *device);

typedef struct {
    const char *name;
    const char *compatible;
    HdfProbe probe;
    HdfStart start;
    HdfStop stop;
} HdfDriver;

void HdfInitialize(void);
HdfStatus HdfRegisterDevice(const HdfDevice *device);
HdfStatus HdfRegisterDriver(const HdfDriver *driver);
HdfStatus HdfStartDrivers(void);
void HdfStopDrivers(void);

#ifdef __cplusplus
}
#endif

#endif