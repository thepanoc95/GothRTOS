# GothRTOS

GothRTOS is a small, statically allocated microkernel written in C11 with a
lua-oriented userspace execution.

> The name just started as a joke and I was like "Huh, why not?" and now there
> it is.

## Kernel hardware drivers

The driver model in `gothrtos/driver.h` provides static, caller-owned
registries, devices, drivers, and bus types. A device has a unique name among
siblings, compatible identifiers, an optional parent bus device, an optional
bus type, and generic resource descriptors. A driver has compatible
identifiers, an optional bus type, and probe/remove callbacks. Register both
with a registry; registering a driver probes previously registered devices,
and registering a device probes against registered drivers.

The device and driver must either use the same bus-type pointer or both use
`NULL`. A bus type can supply a `match` callback to implement bus-specific
matching; without one, compatible identifiers are matched by string equality.
This lets firmware-derived identifiers such as FDT `compatible` values be
used without coupling the kernel core to an FDT parser. If several drivers
match, the first registered driver is selected. Parent devices form the device
tree independently of the bus type used to match the child.

Bus/device drivers can walk direct children with
`goth_device_child_count()` and `goth_device_child_at()` (in registration
order), or use `goth_device_find()` for a child's name. Read a device's
resources with `goth_device_get_resource()`, whose index is the zero-based
index among resources of the requested type.
Resource `start` and `size` describe the range or identifier in the resource's
address space; `flags` is available for platform- or driver-defined attributes.
The registry, devices, drivers, strings, compatible lists, and resource arrays
and bus-type descriptors must remain alive and unchanged while registered;
bus types are assigned before registration.

Probe callbacks may set `device->driver_data`; a failed probe clears that
pointer and leaves the device registered but unbound. The registration call
returns the probe error, and `goth_driver_registry_probe()` can retry unbound
devices. A failed remove leaves that device bound and prevents its
unregistration; driver unregistration also stops at the failure, leaving the
driver registered (earlier devices in the same operation may already have
been unbound). A device with children cannot be unbound, and a bus cannot be
unregistered until all children have been removed. The framework does not
allocate memory, map resources, enumerate firmware, or implement board-specific
drivers; those remain the responsibility of platform code and drivers.

```c
static const char *const uart_compatible[] = {"vendor,uart-v1"};
goth_driver_registry_t registry;
goth_device_t uart_device;
goth_driver_t uart_driver;

goth_driver_registry_init(&registry);
goth_device_init(&uart_device, "uart0", uart_compatible, 1U, NULL,
                 uart_resources, uart_resource_count);
goth_driver_init(&uart_driver, "uart", uart_compatible, 1U,
                 uart_probe, uart_remove);
goth_device_register(&registry, &uart_device);
goth_driver_register(&registry, &uart_driver);
```

The current syscall and GOTH95 program-image contracts are documented in
[docs/ABI.md](docs/ABI.md).

## Kernel task management

Kernel-owned task records remain caller-allocated. `goth_task_find(id)` returns
a registered task record for management and inspection, or `NULL` when the ID
is zero, absent, or the kernel is not initialized. `goth_task_set_priority()`
updates a registered, non-terminated task; its priority is observed at the
next cooperative scheduling point. A running task is not preempted by a
priority change. The scheduler continues to choose the highest-priority ready
task and round-robins tasks of equal priority.
