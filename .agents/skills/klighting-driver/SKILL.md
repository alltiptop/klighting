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
  See the [module conventions](../../../src/drivers/README.md) and each module's own README.
  That directory owns every model-specific artifact: protocol and discovery,
  geometry and UI descriptions/assets, metadata, defaults and calibration, translations,
  documentation, dependency/build/install rules, migration data, tests, fixtures and benchmarks.
- **Ownership boundary:** A driver depends on the public host contract; the host must
  not include its private headers, link its implementation or identify a model in
  rendering, effects, presets, defaults or tests. Shared UI interprets declared
  capabilities and descriptions; device-specific refinements and values come from
  the driver. Common policy defaults remain in the host. This keeps hardware knowledge
  together and lets independent model contributions touch disjoint files.
- **Integration:** Shared build, installation and CI discover driver-owned declarations
  through generic mechanisms. Do not add a model to central target/artifact lists,
  dependency lists, catalogs or documentation inventories. Use `klighting_register_driver`,
  module-local `dependencies.arch`, ordinary CMake install rules and the generic
  `local_bundle` target; see [in-tree modules](../../../docs/device-drivers.md#in-tree-modules).
  Keep module IDs, CMake
  targets, resources and installed paths namespaced. Driver installation rules own
  device files and legacy aliases; shared installers perform the common installation.
  Shared contract tests use synthetic devices, never a concrete model's implementation.
- **Scope:** Adding a model supported by the existing API should change only its own
  directory. A missing capability requires a separately scoped, model-independent
  extension of the contract/infrastructure, not a vendor conditional in the host.
  Do not turn a driver-only request into an unrequested host refactor.
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
  from an actual hardware frame. Document protocol-specific commit and recovery
  behavior inside the module; do not assume other protocols match.
- Keep driver options in `DeviceInstance::options`, shared by backend and controls.
  For legacy settings, inspect [driver_loader.cpp](../../../src/driver_loader.cpp) and
  [application_settings.cpp](../../../src/application_settings.cpp): metadata declares
  imported options, discovery only reads them, and service Reload owns migration.
  Do not introduce a KDE dependency into the driver to read host configuration.

For isolated validation, `AMBIENT_LED_DRIVER_PATH` replaces normal plugin search paths.
Use module-local tests/fixtures for hardware behavior; [host contract tests](../../../tests/test_drivers.cpp)
and [synthetic modules](../../../tests/fixtures/) cover the shared interface. Build/check
commands are in [CONTRIBUTING](../../../CONTRIBUTING.md). For integration changes, check
that the host and its tests build without the model module, and that a new independent
module is discovered, tested and included in installation without central edits.
Test changed behavior, including failure/recovery where affected. Fake transports do
not establish physical mapping or firmware behavior; hardware tests require the device
owner's authorization.

Report the module/layout changes and verification results, distinguishing simulated
behavior from observations on hardware.
