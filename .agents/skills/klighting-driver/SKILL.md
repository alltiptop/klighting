---
name: klighting-driver
description: Guides adding or changing klighting hardware drivers, discovery, layouts and controls. Not for built-in effects, user preset editing or general Qt review.
---

# Device drivers

This skill is repository-local; resolve links relative to its installed directory.
Identify the verified protocol, capabilities and device identity before choosing a
driver boundary. Use the [public contract](../../../src/device_driver.h) and the
relevant sections of the [driver guide](../../../docs/device-drivers.md).

- Keep a model or verified compatible family in `src/drivers/<vendor-model>/`.
  Standard-protocol adapters may span models; vendor names alone prove nothing.
  The [G615LW module](../../../src/drivers/asus-g615lw/CMakeLists.txt) is a model-specific
  example, while [Linux LEDs](../../../src/drivers/linux-leds/README.md) and
  [HID LampArray](../../../src/drivers/hid-lamparray/README.md) show generic adapters.
  Protocol, geometry, metadata and translations stay inside the driver directory.
- Implement the current versioned Qt plugin API. Drivers consume composed frames
  through `DeviceBackend`; presets, effect computation and KWin types stay outside
  the driver. Native modules require compatible API and Qt/compiler ABI, not host
  recompilation for each new device. They are trusted native code, not sandboxed extensions.
- Keep construction and `discover()` bounded and read-only, without claiming hardware.
  Return stable driver/instance/model/element identities; model and instance are not
  interchangeable. Describe only verified geometry and capabilities. Use `DeviceClaims`
  for competing adapters and the existing options for input codes, brightness steps
  and channel limits; do not add model-specific branches to the editor.
  [DeviceManager](../../../src/device_manager.cpp) replaces a backend when its options
  or description change; keep discovery results stable when the device is unchanged.
- Use one backend per enabled instance. Keep blocking hardware I/O off the service
  event thread; deliver callbacks on the owning Qt thread. Bound pending work and
  stop workers before destroying their objects.
  Emit `readinessChanged` for availability or external control changes requiring
  fresh output, never ordinary frame completion. Recover static output without
  depending on animation ticks. Frames contain final software RGB; lists follow
  description order, with `uniform` as fallback. Preserve separate hardware brightness,
  hardware Off and restoration behavior.
  Optional `DeviceControls` serves explicit settings without starting streaming;
  its callback/write lifetime contract is in the public header.
- When suppressing duplicate writes, compare effective hardware output and remember
  it only after success. Invalidate it when failure or reopening makes device state
  uncertain. Skipped writes must still complete queued work; distinguish completion
  from an actual hardware frame. See the [G615LW output notes](../../../src/drivers/asus-g615lw/README.md#access-and-operation)
  for its full-frame commit and recovery behavior; do not assume other protocols match.
- Keep driver options in `DeviceInstance::options`, shared by backend and controls.
  For legacy settings, inspect [driver_loader.cpp](../../../src/driver_loader.cpp) and
  [application_settings.cpp](../../../src/application_settings.cpp): metadata declares
  imported options, discovery only reads them, and service Reload owns migration.
  Do not introduce a KDE dependency into the driver to read host configuration.

For isolated validation, `AMBIENT_LED_DRIVER_PATH` replaces normal plugin search paths.
Use [driver tests](../../../tests/test_drivers.cpp), [fake modules](../../../tests/fixtures/)
and the relevant protocol fixtures; build/check commands are in
[CONTRIBUTING](../../../CONTRIBUTING.md). Test the changed contract, including failure
and recovery where affected. Fake transports do not establish physical mapping or
firmware behavior; hardware tests require the device owner's authorization.

Report the module/layout changes and verification results, distinguishing simulated
behavior from observations on hardware.
