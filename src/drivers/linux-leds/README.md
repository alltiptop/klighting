# Linux LED class

Generic support for keyboard backlights exposed through `/sys/class/leds` and
RGB entries (including subsets such as red/blue) with `multi_index` /
`multi_intensity`. Discovery reads files only.
The kernel driver must already support the hardware. Caps Lock and other monochrome
status indicators are excluded. Zoned keyboard entries appear individually.

- `brightness` / `max_brightness`: whole-keyboard brightness, with an ANSI-style
  schematic and no per-key selection. This is not a detected physical layout.
- RGB: channel order and optional `multi_max_intensity` come from sysfs. Colors
  change through `multi_intensity`; KDE's hardware brightness stays independent.
- Monochrome output maps 0–100% to the full `max_brightness` range, rounded to the
  nearest level; external Off remains Off. Sysfs does not
  identify writers: an external change equal to our last value is indistinguishable.
- Disabling restores session-owned values and the previous LED trigger, preserving
  detected external changes. I/O runs on a worker and pending frames are coalesced.

If direct brightness writes are denied, the driver uses logind's session
`SetBrightness("leds", name, level)` API. Logind checks the active session's access;
errors remain visible in the service status. RGB intensity and trigger attributes
still need direct write access. The plugin does not install permissions, elevate
privileges or modify the system service configuration.
Software tests use temporary sysfs fixtures; physical devices have not been validated.

Alternative drivers remain visible in **Devices**. Disable the current owner before
enabling another driver for the same resource. Layout customization is described in
the [generic layout guide](../generic/README.md).

Protocol: [LED class](https://docs.kernel.org/leds/leds-class.html),
[multicolor](https://docs.kernel.org/leds/leds-class-multicolor.html).
