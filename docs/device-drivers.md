# Writing a device driver

- [Plugin and discovery](#plugin-and-discovery)
- [Layout and capabilities](#layout-and-capabilities)
- [Strip display layout](#optional-strip-display-layout)
- [Frames and controls](#frames-and-hardware-controls)
- [Presets and layers](#presets-and-layers)
- [Installation and validation](#installation-and-validation)
- [External build](#minimal-external-build)

The public contract is [`src/device_driver.h`](../src/device_driver.h): Qt Core/Gui
and C++20, independent of KWin, KDE settings, effects, presets and asusd. Effects
remain built into the host. Each driver module can enumerate multiple physical
devices; enabled devices run together, with independent backends and presets.

## Plugin and discovery

Implement `Ambient::DeviceDriver` on a `QObject`, declare
`Q_INTERFACES(Ambient::DeviceDriver)` and
`Q_PLUGIN_METADATA(IID AMBIENT_DEVICE_DRIVER_IID FILE "driver.json")`.
Metadata must contain `"ApiVersion": 4` and a unique `"Id"` (1–64 ASCII letters,
digits, `_` or `-`). The IID is `org.ambientled.DeviceDriver/4.0`. Build a `MODULE`
library with AUTOMOC, C++20, position-independent code and Qt6::Core/Qt6::Gui.
See the [ASUS G615LW driver](../src/drivers/asus-g615lw/asus_driver.cpp). An external driver
can be built independently; the host does not need recompilation.

Keep each model or verified hardware-compatible family in its own directory,
such as `src/drivers/asus-g615lw/`, including its protocol, layout and translations.
Sharing a vendor name alone does not imply compatible lighting hardware. Metadata
`Name` carries the full human-readable device name;
keep `Id` stable because saved device configurations reference it.

Standard-protocol adapters can instead span models: see [Linux LED class](../src/drivers/linux-leds/README.md)
and [USB HID LampArray](../src/drivers/hid-lamparray/README.md). They use the same API;
specialized drivers can continue to supply verified geometry and device behavior.

This is a versioned C++/Qt interface, not a compiler-independent C ABI. Build
against the same API and compatible Qt/compiler ABI. API 1/2/3 plugins are not loaded
by API 4 hosts; rebuild external drivers against the current header. Never overwrite a mapped library in place.

`discover(DeviceEnvironment)` returns `QList<DeviceInstance>`. It must be bounded,
read-only and must not claim interfaces, start workers or write hardware. The
host supplies the DMI board name; additional discovery belongs to the driver.
Plugins may maintain a read-only discovery cache on their own worker. An optional
QObject property `bool discoveryPending` tells the settings host and probe to
refresh that cache after asynchronous discovery. Keep it true until the next
`discover()` returns the completed result. This does not change the API 4 ABI.
A device instance contains:

- `id`: stable identity **within the driver**, up to 256 characters, for example a
  serial number. Do not use the transient USB bus/address. Use a stable physical
  path when no serial exists; moving such a device to another port changes its
  identity. Duplicate IDs from a driver are rejected rather than guessed.
- `description`: the device model/layout and capabilities.
- `options`: driver-owned parameters passed to its backend/controls. The optional
  `InputKeyCodes` map supplies decimal key IDs as strings and Linux evdev codes as
  integer values, e.g. `{ "36": 17 }` for a device whose W key has ID 36. The editor
  uses this only to outline held keys in Live Preview, without changing selections
  or LED output. Missing entries have no input outline; labels are never used to
  guess codes. The map uses the existing API 4 options container, so older drivers
  remain compatible. Keyboard events come from the settings window, not a specific
  physical input device.
  Do not put credentials in identifiers or display names.

`options.BrightnessSteps` is the number of nonzero hardware brightness levels,
excluding Off. The Devices page offers the Ambient enable switch for RGB devices
or monochrome devices with at least four steps. Unknown monochrome ranges do not
advertise that switch. Effects remain assignable: monochrome output is converted
using the selected brightness channel and rounded to the nearest supported level.
The brightness editor snaps to those levels for a single device or a selection
with matching ranges; mixed ranges keep the shared 0–100% scale.

`options.ColorLevels` optionally maps `red`, `green`, and `blue` to the number of
nonzero channel levels (1–65535). In a provided map, missing or zero channels are
unavailable; e.g. `{ "red": 255, "blue": 255 }` describes a red/blue device.
Without the map RGB is unrestricted. The host adapts the composed output, never
the saved effect. RGB hardware brightness is independent of these channel limits.
Both optional fields use the existing API 4 container; no ABI change is required.

`options.DeviceClaims` optionally lists shared physical resources, such as
`usb:<canonical USB device sysfs path>` or `internal-keyboard`. Alternative drivers
must use the same resource name. Devices remain discoverable; settings prevent
simultaneous enablement across drivers, and the runtime rejects conflicting enabled
entries. Independent zones from one driver can coexist. For existing API 4 plugins,
an instance named `internal` with keyboard geometry implicitly claims
`internal-keyboard`. Generic discovery uses the fixed/removable USB attribute and
platform keyboard LEDs for this association; unknown wiring cannot be inferred.

The host keys saved devices by `(driver ID, instance ID)`, independently of the
model ID. One model may have many instances. Discovery is refreshed every two
seconds by the effect and by Refresh/Add in settings. Existing backends survive
unrelated discovery changes. An unavailable device remains in the saved list;
when its driver detects it again, its enabled backend is recreated. A backend
must also handle readiness/recovery while its discoverable instance remains.
Newly found hardware is only added by the user. Legacy settings without a device
list retain one matching device. An explicit empty list means no devices.

## Layout and capabilities

`DeviceDescription` contains:

- `id`: stable model/layout ID (1–64 ASCII letters, digits, `_` or `-`). Namespace
  it by vendor/model; incompatible layouts must not share a model ID.
- `name`, complete `keys` geometry/labels, optional named `groups`.
- Optional `bodyColor` and `keyColor`: physical surface colors used only by the
  editor. Default-constructed (invalid) `QColor` values use the UI theme. They do
  not affect LED colors, brightness, or saved presets.
- `strips`: ordered strips, each with a group ID, name and an ordered element list.
  The list follows physical order for gradients, independently of hardware IDs.
- `capabilities`: Brightness, Color, PerKeyColor, Lightbar, LightbarPower,
  PerStripColor. PerKeyColor and PerStripColor require Color; LightbarPower
  requires Lightbar. A strip-only device may have no keyboard keys.

Key and strip element IDs share the range 0–65535 and must be unique within the
device. Strip group IDs use 65536–65599. At most 1024 keys, 1024 strip elements
and 64 strips are accepted. Key rectangles use device-local units. Without a display path, a strip appears as
a continuous gradient line below the keyboard. Element rectangles are unused.
Include all keyboard geometry even for brightness/global-color devices: the
editor renders the keyboard with selection disabled. PerStripColor independently
enables element selection, including on devices without per-key keyboard color.

## Optional strip display layout

`DeviceStrip::path` is an optional polyline of `StripPoint { elementId, position }`.
Positions share the keyboard's coordinate system; positive x goes right and positive
y goes down. `pathWidth` is the stroke width in those same units (default 4).
The view fits both the keyboard and any positioned strips, including negative
coordinates. Strips without a path remain straight lines below the keyboard.

Every point references an element in that strip. Between consecutive points, the
preview blends the two element colors linearly. Repeating an element ID at different
positions holds its color over that section. Place transitions close together for
short corner illumination or far apart for long diffused sides. Repeat the first
point at the end to close a loop. This is display metadata only: it changes neither
hardware addressing nor the element order used by saved gradients.

A nonempty path needs at least two points and must include every element. The loader
accepts at most 8192 points across a device, finite coordinates within ±100000,
a finite width in (0, 1000], and no coincident adjacent points. The UI selects the
nearest half of each transition; Ctrl-click, Shift-click and Tab/Space retain
individual element selection. Selected sections have a highlight around the
continuous color preview, with no borders between elements.

## Frames and hardware controls

`createBackend(instance)` creates one independently owned runtime object. All
calls run on the owning Qt thread; hardware I/O must not block the compositor.
`start(options)` receives instance options (including any retained legacy options). `setEnabled(false)` pauses output and restores normal operation.
Destruction stops workers and releases hardware. Backends should coalesce frames and avoid reinitializing hardware between colors.

`submit(DeviceFrame)` receives final RGB after layers, gradients and effects:

- `uniform`: fallback color for omitted lists.
- `keys`: empty for uniform output, otherwise exactly `description.keys.size()`,
  in description order, **not indexed by IDs**.
- `strips`: map from strip group ID to colors, in that strip's element order.
  An omitted strip uses `uniform`. PerStripColor backends keep these colors
  independent of keyboard colors and of other strips.

For a legacy uniform lightbar, `uniform` carries its group color; an explicit key
list keeps keyboard colors independent. Brightness-only drivers use the uniform
color's value as brightness. Preserve hardware power/brightness: hardware Off
must remain off. Only one screen capture and one state per active Ambient preset
are used across all devices.

`createControls(instance)` returns optional hardware settings access without
starting streaming. The current controls contract exposes lightbar power only.
Reads report availability; writes occur only on Apply/OK and finish even if the
editor closes. Callbacks run on the owning Qt thread. The host retains separate
pending controls per instance. Driver-specific flags and daemon behavior belong to the driver, not the editor.

## Presets and layers

Devices use the shared Default preset; matching application bindings take priority.
A device absent from the application preset keeps using Default.
Only presets with a matching model ID are applied. Settings for unavailable
devices and unknown element IDs are preserved.

Version 5 keyboard presets (with version 3/4 read compatibility) accept optional `gradients` arrays in the keyboard
object and each layer. A gradient stores `strip`, `from`, `to`, `start`, `end`,
`ambient` and `effect`. Endpoints are stable element IDs; colors are `#AARRGGBB`.
The range includes both ends, may run backwards, and uses linear RGB/alpha
interpolation in encoded sRGB. One-element ranges use the start color.

Within each layer: whole-strip color, then gradients in list order, then explicit
individual colors. Base and at most one matching layer compose: more conditions win,
with the last row breaking ties. Normal notification
overlays follow layers; above-all overlays apply last. Gradients and their effect
references survive file export/import. No host code executes from preset files.

## Installation and validation

The loader scans `ambient-led/drivers/v4` under Qt's plugin paths and
`<executable directory>/lib/ambient-led/drivers/v4`. `AMBIENT_LED_DRIVER_PATH`
replaces these paths with a colon-separated list for development. These directories
contain trusted native code, not sandboxed extensions. Restart/reload the effect
and reopen settings to discover newly installed modules.

A typical Qt 6 installation uses
`/usr/lib/qt6/plugins/ambient-led/drivers/v4/`. Incompatible metadata, invalid layouts
or duplicate driver IDs are reported; unrelated valid drivers still work. Repeated
references to the same canonical library are ignored. Qt pins plugin code for
pending callbacks/metatypes; destroying a backend still releases hardware. The
[local installer](../tools/install-local.sh) replaces its supported modules atomically;
see the bundled driver's notes for its migration behavior.

Tests load the real ASUS description without starting USB, enumerate two synthetic
strips, exercise independent presets/disable/reconnect, verify gradient endpoints,
layer precedence, saved devices, real QML controls and USB packet bytes through
fake transport. These tests do not replace hardware validation for each new driver.

## Minimal external build

Use the installed header or point `KLIGHTING_DRIVER_INCLUDE_DIR` at the host's
`src` directory. The module needs no link to the host, KDE, or KWin:

```cmake
cmake_minimum_required(VERSION 3.25)
project(example_lighting_driver LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui)
qt_standard_project_setup(REQUIRES 6.10)
find_path(KLIGHTING_DRIVER_INCLUDE_DIR device_driver.h PATH_SUFFIXES ambient-led REQUIRED)
add_library(example_driver MODULE driver.cpp)
target_include_directories(example_driver PRIVATE "${KLIGHTING_DRIVER_INCLUDE_DIR}")
target_link_libraries(example_driver PRIVATE Qt6::Core Qt6::Gui)
set_target_properties(example_driver PROPERTIES PREFIX "")
```

Minimal `driver.json` metadata:

```json
{ "Id": "vendor-model", "Name": "Vendor Model", "ApiVersion": 4 }
```

Implement all three `DeviceDriver` methods; return an empty controls pointer when
power controls are unsupported. [The fake multi-device module](../tests/fixtures/multi_driver.cpp)
is a small compilable reference without USB dependencies; replace its discovery
and backend with actual bounded hardware logic. Do not advertise a capability until
it is implemented and verified. Frame lists follow description order, not key IDs.

Bundle the module's dependencies, license and own translations with it. Test discovery,
layout rejection, frame bounds, disabled/off state, disconnect/reconnect and destruction
with pending callbacks. Then validate the physical mapping and restoration separately
with the hardware owner's permission. See [G615LW notes](../src/drivers/asus-g615lw/README.md)
for that driver's transport details, not a universal USB protocol.
