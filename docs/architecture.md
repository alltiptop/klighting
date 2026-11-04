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
| Effects | `effect_palette.*`, `color.*`, `notification_effect.*`, `microphone_effect.*` | Dominant color, smoothing, brightness/temperature, static colors, pulses and mute indicators |
| Platform inputs | `notification_source.*`, `power_source.*` | Asynchronous D-Bus input; authenticate notification sender and track power changes |
| Runtime/service | `lighting_runtime.*`, `lighting_service.*`, `daemon_main.cpp` | Own drivers, presets, composition and timers; reload settings over session D-Bus |
| Source contracts | `desktop_source.h`, `capture_source.h`, `microphone_source.h`, `integration_sources.h` | Independent desktop/capture/microphone states and an optional hardware ownership guard, without platform library types |
| Audio observation | `audio_sources.*`, `pulse_microphone_source.*` | Read-only default-input mute observation over the PulseAudio protocol; private static target linked only into the daemon |
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

A device chooses a matching application preset or the shared Default, then selects its profile
by driver and instance identity. A preset can contain up to 32 independent device
profiles, including several instances of the same model. Base is always the
underlay; at most one matching keyboard layer is selected. The combination with the
most conditions wins, with the last row breaking ties. Effects blend in list order,
including the [above-layer group](settings.md#effect-presets).
Local effect IDs are scoped by device profile at runtime.

Device management retains each resolved profile/layer, its styles, assignments and used
effect IDs. Observed window/key state is always updated; unchanged selection reuses that
result. Configuration and power changes invalidate it, and discovery replaces a backend
when its options or description change. Ambient, notification and microphone evaluation
share one required-effect set. Microphone input is event-driven and needs no animation
timer. Readiness changes can resend the current frame without resolving the profile again.

Continuous effect states are shared across devices using the same effect. One small
32×18 GPU sample feeds the active Ambient calculations. Readback uses a fence/PBO and
zero-timeout polling; it never waits for GPU completion on KWin's thread. Expensive
capture stops when no unpaused assignment requires it. Static output and settled
Ambient have no continuous animation timer. The runtime schedules pending samples,
transitions and final output independently of capture, composing only when output,
configuration or device readiness changes. Drivers coalesce and deduplicate frames
and perform USB work off the service event thread. Shutdown must stop workers before
releasing driver objects.

The capture timer runs only for pending readback, a deferred screen change or an
outstanding repaint request. Valid outermost screen paints wake capture after idle;
changes before the sampling deadline or while readback is busy retain one pending
capture. A successful capture consumes the change, so its own repaint does not keep
sampling alive. A forced repaint waits up to one additional 16 ms readback-poll
interval so normal animation paints can supply the frame first. Repaint requests
without a usable target retry at most four times per second. This does not change
the existing direct-scanout restriction.

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

The adapters retain successful configuration per provider instance and D-Bus owner.
One request is in flight per adapter; newer settings replace the pending desired
state. Errors/timeouts retry on the existing two-second health poll. Unavailability,
owner replacement or a new `instanceId` invalidates delivery, including a plugin
reload inside the same KWin process. Original protocol-1 providers without a valid
instance ID retain periodic configuration for recovery. Health/legacy-controller
polling remains separate; it does not implicitly retry a failed GPU capture.

### Compositor failure containment

Native KWin plugins share the compositor process. A segmentation fault, abort or GPU
driver failure in that process cannot be safely recovered with a C++ exception handler.
Keep hardware access, effect composition, configuration parsing and blocking work in
`klightingd`, not in the compositor adapters.

Server plugins link with `-z defs` and `-z now`: missing dependencies fail the build,
and symbols are resolved at load time rather than on the first rendered frame.
`klighting-plugin-check` tests each binary in a separate unprivileged process, with a
five-second deadline and core dumps disabled. It loads dependencies without creating
plugin instances. The local installer runs this check before replacing any files.
This catches loading failures, not faulty rendering or arbitrary native-code bugs.
The settings controller disables Ambient startup before a load/unload request and
re-enables it only after KWin confirms loading. Capture errors stop further sampling until an explicit retry.

Future Wayland/X11 providers should use public protocols from a client process, with
bounded asynchronous IPC to the lighting runtime. If a server plugin is unavoidable,
keep it limited to observation/capture, use the same strict/eager linking and isolated
load checks, and never load hardware drivers there. A fully isolated capture client
(for example a portal/PipeWire provider) would remove klighting native code from the
compositor; it is not implemented yet.

## Persistence and UI

Settings remain pending until Apply. JSON lives under the standard configuration root
in `kwin-effect-ambient-led/`; the retained name is a compatibility identifier.
`settings-v2.json` references immutable content-addressed files in `presets/` and
`effects/`. `QSaveFile` publishes each file and the index atomically; a nonblocking
`QLockFile` serializes conflict detection, publication and cleanup. The editor supplies its
previous snapshot for comparison under that lock. This is not a multi-user database transaction.

Multi-device JSON uses version 7: layer conditions belong to the preset, with aligned
assignments in each device profile. One matching layer is selected for every device;
unassigned elements inherit Base. Readers migrate version 6 device layers and retain
single-device versions 3–5.
Device cards share one pending document, selected layer and live layer preview;
element selections remain independent. The preset sidebar lists Base as the preset
root and modifier layers as children; selection is editor state, not saved configuration.
Copy/paste matches keyboard input codes or unambiguous labels and strip positions;
unsupported assignments are omitted after a capability-loss warning. Existing
strip gradients remain editable, although new gradients have no creation button.
Driver metadata/translation resources are isolated from shared application catalogs.
Residual application settings live in `klighting/application.json`; legacy driver
options live in `klighting/drivers/<driver-id>.json`. `application_settings.*` reads
the common envelope; driver metadata declares imported keys. Only service Reload
publishes migrations, atomically per file with a completion marker. Discovery and UI
merge legacy values in memory until migration; explicit new values win and `kwinrc`
remains untouched. The common loader supplies backend and controls through
`DeviceInstance::options`, without driver-specific branches in the host.

The installed effect ID `ambientled`, KCM IDs, configuration directory and KDE menu
**Lighting** remain stable independently of the project name `klighting`.
