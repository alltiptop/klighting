# USB HID LampArray

Experimental generic support for the HID Lighting and Illumination page (`0x59`).
No vendor or model table is used. The plugin parses the actual HID report descriptor,
including report IDs, field offsets, channel limits and Feature/Output report types.

Discovery only reads sysfs and cached metadata. On first enable, the worker claims
the dedicated lighting interface, queries lamp attributes, and caches their geometry
under `$XDG_CACHE_HOME/klighting/hid-lamparray/`. Until then the editor shows a
nonselectable schematic. Refresh Devices after the first successful activation to
load the detected layout in the editor. Runtime discovery refreshes automatically.

Keyboard control LEDs use reported coordinates and HID key bindings. Keycaps are
schematic squares: HID supplies centers, not outlines. Accent LEDs appear as a
numbered strip. An optional [layout JSON](../generic/README.md) can refine the drawing.

Limits of this initial backend:

- USB only; Bluetooth transport is not implemented.
- At most 1024 lamps, 4096-byte reports and a minimum update interval of at most 1 s.
- A dedicated interface without input reports and with autonomous mode control is
  required. Shared keyboard/mouse interfaces are not detached.
- HID descriptors with unsupported encodings are rejected. Manufacturer-specific
  lighting protocols still require a specialized plugin.
- The user needs access to the USB device. No permission rules are installed.

USB transfers have 250 ms timeouts and run outside KWin's thread. Frames use
MultiUpdate or RangeUpdate and commit once, respecting the device's minimum interval.
Disabling restores autonomous mode and reattaches the kernel driver. A crash or USB
failure can prevent restoration. Where Linux exposes a matching keyboard brightness
control, hardware Off is honored; LampArray itself has no separate brightness getter.
The plugin never opens hidraw nodes.

A specialized driver can provide verified geometry and behavior for its supported
models. Select a generic alternative only after disabling the existing owner.
Protocol/parser and discovery tests use synthetic fixtures; no physical LampArray
device has been tested with this backend yet.

Protocol: [USB HID Lighting and Illumination](https://www.usb.org/sites/default/files/hutrr84_-_lighting_and_illumination_page.pdf).
