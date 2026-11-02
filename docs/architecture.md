# Architecture

klighting is a native KWin effect with a shared Qt Quick/Kirigami settings editor.
Effects stay built into the host; device drivers are independently loadable Qt plugins.

| Area | Entry points | Responsibility |
| --- | --- | --- |
| Driver contract | `src/device_driver.h` | Device discovery, geometry, capabilities, frames and explicit controls; Qt Core/Gui only |
| Device management | `driver_loader.*`, `device_manager.*` | Validate modules/layouts, preserve per-instance settings, own backends and apply power policy |
| Preset data/storage | `presets.h`, `presets.cpp` | Bounded JSON validation, references, import/export and atomic file publication |
| Preset evaluation | `preset_runtime.cpp`, `keyboard_profile.*` | Key conditions, one active layer, assignment expansion and shared/local references |
| Effects | `effect_palette.*`, `color.*`, `notification_effect.*` | Dominant color, smoothing, brightness/temperature, static colors and pulses |
| Platform inputs | `notification_source.*`, `power_source.*` | Asynchronous D-Bus input; authenticate notification sender and track power changes |
| KWin adapter | `ambient_effect.*`, `frame_sampler.*` | Window/input/output changes, lock state, scheduling and small asynchronous GPU captures |
| Settings facade | `settings_controller.*` | Pending collection, presets, layers, application rules, load/save and preview |
| Effect editing | `settings_effects.cpp` | Shared/local effects, ordered assignments and retained legacy gradients |
| Device settings | `settings_devices.cpp` | Selection/discovery and asynchronous lightbar controls |
| KDE integration | `ambient_config.*`, `effect_control.*`, `ui/` | KCM state, effect enablement and one editor shared by native sections/standalone host |

The public `DeviceDriver` interface is the extension boundary. Its API version and
Qt/compiler ABI must match; native modules are trusted code. No effects/settings
headers or KWin types are required in a driver. See the [driver guide](device-drivers.md)
for ownership, discovery limits and a standalone build example.

## Composition and scheduling

A device chooses a matching application preset or its default. Base is always the
underlay; at most one matching keyboard layer is selected. The combination with the
most conditions wins, with the last row breaking ties. Effects blend in list order;
above-layer notifications compose last. Local effect IDs are scoped by preset at runtime.

Continuous effect states are shared across devices using the same effect. One small
32×18 GPU sample feeds the active Ambient calculations. Readback uses a fence/PBO and
zero-timeout polling; it never waits for GPU completion on KWin's thread. Expensive
capture stops when no unpaused assignment requires it. Static output has no continuous
animation timer. Drivers coalesce and deduplicate frames and perform USB work off the
compositor thread. Shutdown must stop workers before releasing driver objects.

## Persistence and UI

Settings remain pending until Apply. JSON lives under the standard configuration root
in `kwin-effect-ambient-led/`; the retained name is a compatibility identifier.
`settings-v2.json` references immutable content-addressed files in `presets/` and
`effects/`. `QSaveFile` publishes each file and the index atomically; a nonblocking
`QLockFile` serializes conflict detection, publication and cleanup. The editor supplies its
previous snapshot for comparison under that lock. This is not a multi-user database transaction.

Keyboard JSON currently uses version 5; readers retain versions 3 and 4. Existing
strip gradients remain editable, although new gradients have no creation button.
Driver metadata/translation resources are isolated from shared application catalogs.
The only legacy ASUS option injected by the host is `AuraObjectPath`; new driver
options belong to `DeviceInstance::options`.

The installed effect ID `ambientled`, KCM IDs, configuration directory and KDE menu
**Lighting** remain stable independently of the project name `klighting`.
