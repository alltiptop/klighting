# ASUS ROG Strix G16 G615LW

This driver is intentionally limited to the verified G615LW board and ASUS
`0b05:19b6` lighting controller. A vendor name or matching GPU is not sufficient
proof of hardware compatibility. Protocol, discovery, layout, metadata and
translations, tests, dependencies and installation declarations live in this directory.
The build requires `libusb-1.0` via pkg-config (Arch: `libusb`; Debian/Ubuntu:
`libusb-1.0-0-dev`; Fedora: `libusb1-devel`) and Qt DBus.

The controller has 88 keyboard/control lamps and 12 decorative lamps. Discovery
uses DMI and asusd metadata; the USB worker validates the LampArray descriptor,
lamp purposes and coordinates before per-element output. The manually authored
preview contour places elements 2/5/8/11 at the upper-left/lower-left/lower-right/
upper-right corners. It describes light diffusion, not a physical scale drawing.

## Access and operation

The logged-in user needs access to the matching `/dev/bus/usb` node, not only
hidraw. Existing distribution/OpenRGB udev rules may already provide `uaccess`.
Inspect the current ACL before changing it. Do not run KWin as root or grant
world-writable access to all USB devices. No OpenRGB process is required.

asusd remains responsible for hardware brightness, power settings and factory
modes. Streaming colors use HID LampArray through libusb: range updates for
uniform/ranged colors, multi-updates for scattered keys, and one final commit
per frame. Intermediate writes do not blank the strip. Enabled lamps use intensity
255; hardware Low/Med/High already scales brightness. Hardware Off stays off.

USB work runs on a worker thread, coalesces pending frames and bounds transfers.
The client first drops identical logical frames. The worker also compares the final
RGB/intensity values for all 100 lamps, so changes in disabled zones or equivalent
color representations do not cause USB writes. Only a successful final commit is
remembered; errors, release and reopen clear that state. First and recovery frames
still update the complete device. Completed-frame counters exclude skipped duplicates.
The host's lightbar-off policy can set every strip element black independently of
pausing effects. Explicit lightbar power edits send only the awake flag for zone 2
through asusd and preserve other power flags. Ambient frames do not persist colors
into daemon configuration files.

On lock, disable or unload, the worker releases the interface and the client attempts
to restore the saved factory Aura mode. Restoration is skipped at brightness Off:
asusd's raw method can raise its cached Off brightness. A concurrent external
brightness change remains a daemon race. A process crash cannot guarantee interface
reattachment or mode restoration.

Do not run competing RGB controllers during streaming. Wine/Proton or other HID
clients may also interfere with this interface on affected kernels. The driver does
not modify kernel, Wine or other applications' settings. A successful USB transfer
is not evidence that every physical lamp displayed the intended color.

The [local installer](../../../tools/install-local.sh) atomically replaces the stable
effect/ASUS driver filenames and retires older v1/v2/v3/v4 ASUS copies, preserving
third-party modules. This module declares its aliases in [install/legacy-plugins.txt](install/legacy-plugins.txt).

## Diagnostics

`ambient_probe` without a test flag only lists discovered devices. Explicit tests
must be requested by the device owner, after stopping `klightingd` and any legacy
lighting-owning effect:

```sh
./build/ambient_probe --test-colors --device asus-g615lw:internal
./build/ambient_probe --test-keys --device asus-g615lw:internal
```

Use the identity printed by discovery. Tests send colors for 12 seconds at up to
20 FPS, then request restoration. The element test uses repeating red/green/blue
in driver order, with changing intensity. These direct tests bypass Ambient's
software brightness limits. No source test suite invokes them.

See [the driver API](../../../docs/device-drivers.md) for host integration and
[tests/fixtures/g615lw-lamps.json](tests/fixtures/g615lw-lamps.json) for the
public model-level lamp descriptor fixture; it contains no device serial number.

With `BUILD_TESTING=ON`,
`./build/src/drivers/asus-g615lw/tests/asus_g615lw_output_benchmark` compares nine output scenarios
using an in-memory report sink, three runs each. JSON includes completed operations,
committed frames, report bytes and process CPU time. It checks all final lamp values
without accessing USB or D-Bus. Use identical build/scenario settings for before/after
comparisons; these counters do not measure bus latency, firmware behavior or energy.
`asus_g615lw_output_tests` covers deduplication and failed/partial commits;
`asus_g615lw_transport_tests` also checks queue progress and rate limiting through
a fake asynchronous transport. Run all module checks with
`ctest --test-dir build -R asus_g615lw --output-on-failure`. Layout, asusd controls
and translations have separate tests here; shared settings tests use synthetic hardware.

## Driver configuration

`AuraObjectPath` selects one compatible asusd Aura object when discovery finds more
than one. Leave it empty for automatic selection. The driver declares this legacy
option in its metadata; service Reload imports it from `kwinrc` once into
`$XDG_CONFIG_HOME/klighting/drivers/asus-g615lw.json` (normally under `~/.config`):

```json
{ "version": 1, "legacyImported": true, "values": { "AuraObjectPath": "" } }
```

Set the value to the exact object path reported by asusd when disambiguation is
necessary. Streaming and settings controls receive the same option. Opening settings
or listing devices does not write this file; an explicitly set new value takes
priority over the old `kwinrc` value. The [configuration notes](../../../docs/install.md#application-configuration)
describe migration, validation and retry behavior.

## Layout metadata

ASUS G615LW keeps key IDs 0–87 and exposes the lightbar group as 65536. Its twelve
ordered elements have hardware IDs `89,91,93,95,97,99,98,96,94,92,90,88`, following
controller coordinates around the chassis. The driver maps this order back to
hardware lamp IDs and verifies live coordinates before per-element output.

The G615LW supplies a manually authored, compact top-view schematic around its
keyboard. Display elements **2, 5, 8, 11** are the upper-left, lower-left, lower-right
and upper-right corners. Each corner has approximately 10 units of local spread;
the other two elements on each side blend across that side. This describes observed
light diffusion, not a scale drawing of the chassis or firmware lamp coordinates.

The preview body is `#333333`; key surfaces are `#111111`. These colors are
editor metadata and do not affect hardware output.
