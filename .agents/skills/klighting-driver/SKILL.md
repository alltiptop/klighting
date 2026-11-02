---
name: klighting-driver
description: Guides klighting driver changes for device discovery, layouts, capabilities and controls. Use for adding or modifying hardware support, not preset editing or general Qt review.
---

# Device drivers

This skill is repository-local; resolve links relative to this directory.
Identify the requested model, verified protocol and capabilities before choosing a driver boundary.

Start with the [driver API guide](../../../docs/device-drivers.md) and
[public header](../../../src/device_driver.h). The host loads native Qt plugins;
new supported layouts do not require changes to the effect or editor.

- Keep a model or verified compatible family in `src/drivers/<vendor-model>/`.
  The [G615LW module](../../../src/drivers/asus-g615lw/CMakeLists.txt) shows the build;
  its metadata, protocol, layout and translations are local to that directory.
- Implement `DeviceDriver`, with bounded read-only `discover()`, one `DeviceBackend`
  per instance, and `DeviceControls` for supported explicit controls. Keep hardware
  I/O off the compositor thread. The API guide defines callback and teardown ownership.
- Choose stable driver, instance and element IDs. Describe capabilities and geometry;
  the editor renders them. Do not infer per-key or strip support from a vendor name.
- Point `AMBIENT_LED_DRIVER_PATH` at the built plugin directory for isolated development.
  [Driver tests](../../../tests/test_drivers.cpp) and
  [fake modules](../../../tests/fixtures/) demonstrate discovery and frame contracts.

Build and verification commands are in [CONTRIBUTING](../../../CONTRIBUTING.md).
Fake-transport tests prove software behavior; physical mapping and restoration need
an explicitly requested device test. This skill covers integration, not general Qt style.

Deliver the driver module, metadata/layout and relevant verification results. State
which capabilities were tested with fake transport and which were observed on hardware.
