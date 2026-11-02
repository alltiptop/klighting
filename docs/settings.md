# Settings

The same editor runs inside System Settings and `kcmshell6 kcm_ambientled`.
**Devices / Presets / Effects / Applications** remain the primary navigation.
At narrow widths KDE collapses the sidebars; child pages stay inside the module.
Changes remain pending until **Apply**; **Reset** reloads saved settings, and
**Defaults** changes pending values for the current section.

## Devices

Add discovered devices individually. Each has an enable switch, a default preset
and an option to follow application bindings for its model. Offline/disabled devices
retain their settings; removing a device does not remove its presets. The G615LW
keyboard and lightbar are parts of one physical device/backend.

Enable **Ambient LED effect** here to run lighting. The host must remain enabled
for static assignments and power policies too. The lightbar checkbox applies the
supported driver's hardware power setting; screen colors are streamed separately.
**Refresh** repeats discovery and reads lightbar settings without applying edits.

Device-wide effect pauses and lightbar-off policies are independent, for battery
power and low charge. Pausing Ambient holds its last color; paused notifications
are transparent. Turning off the lightbar sends black to all its elements even
when effects are paused. The backend stays in control instead of restoring the
factory animation. Returning to AC resumes normal operation.

Low battery follows KDE's `powerdevilrc` `BatteryManagement/BatteryLowLevel`
(default 10%), with UPower's warning level as fallback if percentage is unavailable.
Low charge while connected to AC does not activate battery rules. A UPower outage
retains the last known policy.

## Presets and selection

A keyboard preset belongs to a driver model. It contains Base, key combinations,
assignments, named groups and local effects. Shared effects are referenced by ID.
Export includes all referenced shared definitions, so the resulting JSON bundle is
portable. Import remaps conflicting IDs and preserves unrelated presets.

Select keys by clicking or dragging; Ctrl adds to a selection. The Group selector
includes driver-provided groups, saved groups and strips. Strip elements also accept
Shift-click ranges. With no selection, or all elements selected, the effect list
edits the default layout. Devices supporting only global color/brightness still
show a full keyboard preview with per-key selection disabled.

**Effects** is an ordered list: use **+** to choose a saved effect or create one
local to the assignment. Reorder with the drag handle or move buttons. Effects
blend from top to bottom with opacity. Shared definitions are marked, and editing
their expanded options changes every assignment using them. Local definitions stay
inside this keyboard preset. Colors have a swatch picker and a hex field.

The lightbar uses the same list and layers as keys. Drivers may supply its shape
and element order; otherwise it appears as a line below the keyboard. Existing
saved gradients remain editable/removable; the Add Gradient entry is intentionally
absent pending a general spatial mapping design.

## Layers and Live Preview

Base is the underlay. Add a layer and record its key combination; replacing the
combination replaces it entirely, and duplicates are rejected. Select its radio
button/name to edit it. At runtime only one matching layer is active: the combination
with the most conditions, with the last row winning ties. Unassigned keys inherit Base.
For example, Ctrl+Shift wins over either Ctrl or Shift regardless of list order.

Generic modifiers accept either physical key; left/right-specific conditions remain
available. A lone generic Shift also follows Caps Lock. Extra modifiers prevent that
fallback, so Shift+Ctrl is not the lone Shift layer. Menu/Copilot key behavior depends
on the input events exposed by the hardware; unsupported Fn Lock cannot be inferred.

**Live Preview** is off by default. It temporarily shows the active combination in
the editor and outlines held keys without changing mouse selection. It watches keys
only while the settings window is active; it is not a separate hardware test mode.

## Effect presets

**Effects** contains individual saved effect presets, not a list of available types.
Ambient and Notifications each have independent battery/low-charge pause settings.

- **Ambient:** screen sampling 1–20 FPS (default 3), LED transitions 1–60 FPS
  (default 60), smoothing 0–3000 ms (default 400), minimum/maximum brightness
  (default 25/70%), and adjustable color temperature. A dominant-color histogram
  works on a 32×18 sample; it does not average every screen pixel. A dark screen
  retains the configured minimum, while hardware brightness Off remains Off.
- **Solid Color:** a constant RGB color, composited with the assignment's opacity.
- **Notifications:** color, blink/pulse pattern, period and cycle count. Filters use
  application desktop ID/name, category, event ID and urgency metadata; notification
  bodies are not parsed. Missing metadata cannot establish a match. Multiple
  notification effects can target the same keys. **Show above all layers** places a
  notification last in composition. Power connection/disconnection are also triggers.

Notifications use KDE's notification watcher API over D-Bus. Screen capture is
needed only for unpaused, assigned Ambient effects. If none needs it, readback
resources are released; static colors do not require a continuous animation timer.

## Applications and files

Add an application binding, enter its exact window class, and select a saved keyboard
preset. **Select Active Window** uses KWin's window picker. Matching is case-insensitive;
for Wayland applications the class normally comes from the application ID. The first
matching enabled rule for the device's model wins; otherwise its default applies.
Search fields filter applications, presets and effects without changing selection IDs.

Files live under `$XDG_CONFIG_HOME/kwin-effect-ambient-led/` (normally
`~/.config/kwin-effect-ambient-led/`): `settings-v2.json`, `presets/`, and `effects/`.
These compatibility paths remain unchanged by the project rename. Use Export/Import
for sharing. The index contains local device identities and application bindings;
it is not part of an exported keyboard bundle and should not be published as source.
