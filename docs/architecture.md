# Architecture

klighting runs lighting in the user-session service `klightingd`, with optional KWin
integrations and a shared Qt Quick/Kirigami settings editor.
Effects stay built into the host; device drivers are independently loadable Qt plugins.

| Area | Entry points | Responsibility |
| --- | --- | --- |
| Driver contract | `src/device_driver.h` | Device discovery, geometry, capabilities, frames and explicit controls; Qt Core/Gui only |
| Device management | `driver_loader.*`, `device_manager.*` | Validate modules/layouts, preserve per-instance settings, own backends and apply power policy |
| Preset data/storage | `presets.h`, `presets.cpp` | Bounded JSON validation, references, import/export and atomic file publication |
| Preset evaluation | `preset_runtime.cpp`, `keyboard_profile.*` | Key conditions, one active layer, assignment expansion and shared/local references |
| Effects | `effect_palette.*`, `color.*`, `notification_effect.*` | Dominant color, smoothing, brightness/temperature, static colors and pulses |
| Platform inputs | `notification_source.*`, `power_source.*` | Asynchronous D-Bus input; authenticate notification sender and track power changes |
| Runtime/service | `lighting_runtime.*`, `lighting_service.*`, `daemon_main.cpp` | Own drivers, presets, composition and timers; reload settings over session D-Bus |
| Desktop input contract | `desktop_source.h` | Window class, configured key conditions and lock state, without compositor types |
| KWin desktop integration | `kwin_integration.cpp`, `kwin_desktop_source.*` | The sole current desktop provider; independent of Ambient |
| Ambient capture | `ambient_effect.*`, `frame_sampler.*`, `capture_channel.*`, `kwin_capture_source.*` | Optional small asynchronous GPU captures; no drivers or preset evaluation |
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
service event thread. Shutdown must stop workers before releasing driver objects.

## Process boundaries

`klightingd` owns `org.klighting.Service` on the session bus. Apply calls `Reload`;
`Status` reports runtime health. Only one daemon can own the name. Closing settings
or disabling the `ambientled` effect does not stop device control. Without a capture
source, Ambient holds its last color (initially its configured minimum); static
colors, brightness filters, power policy and notification animation keep working.

The native `klighting_integration` plugin publishes active window class, lock state,
modifiers and only the key codes referenced by layer conditions. `DesktopSource`
is the neutral input boundary: rule matching and layer selection live in the core.
KWin is currently the only implementation. Outside KWin, device defaults remain
active; automatic application/layer switching and lock tracking need a future
provider. Notification input currently uses Plasma's watcher API; power uses UPower.
No additional Wayland/X11 adapters are implemented. The service needs Qt and KF6
Config libraries, but no KWin library or running KDE desktop.

Capture and desktop IPC use versioned objects at `/org/klighting/Capture` and
`/org/klighting/Desktop`. Both send targeted signals only to the daemon's current
unique bus owner. They accept configuration only from that owner; the daemon checks
the current KWin owner and payload bounds. Capture stops on service disconnect.
The daemon waits for its initial desktop/lock lookup and refuses hardware output
while a legacy lighting-owning Ambient effect is loaded, preventing two controllers.

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
