# Architecture

klighting runs lighting in the user-session service `klightingd`, with optional KWin
integrations and a shared Qt Quick/Kirigami settings editor.
Effects stay built into the host; device drivers are independently loadable Qt plugins.

## Source layout

Files are grouped by responsibility. Headers stay beside their implementations;
cross-directory includes name the path below `src/`, for example
`presets/presets.h`. Includes within one directory use the local filename.

| Directory | Responsibility |
| --- | --- |
| `src/config/` | Application settings storage and shared lighting defaults |
| `src/devices/api/` | Public `DeviceDriver` contract: discovery, geometry, capabilities, frames and controls |
| `src/devices/` | Module loading, layout validation, backend ownership and device policy |
| `src/drivers/` | Independently loadable hardware modules with their own tests and resources |
| `src/presets/` | Preset data, JSON validation/storage, profile evaluation and copy/paste transfer |
| `src/effects/` | Color processing, palettes, notification animation and microphone indicators |
| `src/service/` | Daemon entry point, lighting composition, scheduling and service orchestration |
| `src/sources/` | Input contracts, notification observation and power state |
| `src/sources/audio/` | PulseAudio protocol observer and its factory, linked only into the daemon |
| `src/sources/media/` | Optional PipeWire capture and V4L2 camera-use observers, linked only into the daemon |
| `src/sources/kwin/` | Daemon-side desktop/capture clients; no KWin library dependency |
| `src/ipc/` | Shared protocol identifiers, component identity, daemon peer and startup guard |
| `src/kwin/` | Compositor-side desktop plugin and Ambient GPU capture, with their metadata |
| `src/settings/` | KCM templates, settings controllers, effect/device/profile editing and translations |
| `ui/` | Shared Qt Quick/Kirigami editor components |

The existing CMake targets remain the compilation and dependency boundaries.
Organizational directories do not each need another library or `CMakeLists.txt`;
their sources are listed explicitly in the root build file. Driver modules keep
their own build files because they are independently discoverable plugins.
Include paths and dependencies are target-scoped, following the
[CMake buildsystem model](https://cmake.org/cmake/help/latest/manual/cmake-buildsystem.7.html)
and [Qt's CMake guidance](https://doc.qt.io/qt-6/cmake-get-started.html).

Keep platform adapters and editor dependencies out of preset/effect evaluation.
The service consumes source contracts; KWin-specific server APIs stay in
`src/kwin/`. The `ambient_driver_api` target exposes only `src/devices/api/`.
The existing ASUS G615LW backend also uses shared LED timing defaults from
`config/settings.h`, so its target declares that host include dependency explicitly.
The public include spelling `device_driver.h` and installed
`ambient-led/device_driver.h` path remain stable.

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
Local effect IDs are scoped by device profile at runtime. Unassigned elements in a non-default
device Base inherit the matching Default device's standard state, including local-effect
references, gradients and above-layer assignments. Default modifier layers are never inherited.
The selected preset's Base assignments replace only their own elements, and its own layers
and above-layer assignments apply afterward. Explicit empty key or strip lists stay off;
an empty default list leaves unassigned elements inherited. A black solid effect can represent
an intentionally dark preset. Resolution uses the device description to expand strip defaults
and gradients and never materializes inherited assignments in the saved profile.

Device management retains each resolved profile/layer, its styles, assignments and used
effect IDs. Observed window/key state is always updated; unchanged selection reuses that
result. Configuration and power changes invalidate it, and discovery replaces a backend
when its options or description change. Ambient and trigger evaluation share one required-effect set. `EffectTriggers` in
`src/triggers/` decides whether a non-Ambient effect is active independently of its visual
behavior (solid color, brightness or pulse). State triggers stay active while their condition
matches; event triggers have a bounded lifetime and coalesce overlapping events. Static
state-triggered output needs no animation timer. Unknown input never matches, even for
negative conditions. Sources are enabled only while referenced, unpaused effects need them. Readiness changes can resend the current frame without resolving the profile again.

`MediaActivitySource` supplies microphone, camera and screen-capture activity independently
of Ambient's `CaptureSource`. Its PipeWire adapter uses a nonblocking Qt socket notifier,
subscribes to registry/node/link metadata, and waits for the initial round trip before
publishing absence. It never creates streams or links. Running sources with active links
to running consumers determine activity; KWin stream names and explicit camera/screen
metadata distinguish video inputs. Connection loss invalidates the snapshot, and bounded
retries recover while the source is required. An optional V4L2 adapter discovers capture
devices through libudev and checks open descriptors of observable processes belonging to
the current user. It matches character-device numbers, without opening cameras or depending
on model IDs, process names or device paths. A cancellable worker polls once per second,
including device discovery for hotplug, only while a camera trigger is required. Metadata,
output-only, memory-to-memory codec and touch nodes are excluded. The combined source reports
camera use if either adapter confirms it; reporting absence requires valid negative snapshots
from every enabled adapter. Disabling drops snapshots, and generations reject stale worker
deliveries. Service-side caching replays current values
when effect assignments change, without reconnecting or retaining stale state after disable.

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

Effect JSON uses version 2, with an optional top-level `trigger` object. Version-1 microphone
effects migrate to solid color plus the unmuted microphone condition; notification effects
migrate to pulse plus a notification or power-connected event. IDs, colors, filters and total
event duration are retained. Loading migrates in memory, and Apply/export writes version 2.
Older application versions cannot read these new effect files.

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
