# Generic driver support and custom layouts

`linux-leds` and `hid-lamparray` implement the existing API 4 contract. Their shared
support library handles cached asynchronous discovery, coalesced output and optional
display geometry. Hardware protocols remain in their respective plugin directories.
The ASUS model driver does not depend on this library.

Place an optional file at `$XDG_CONFIG_HOME/klighting/layouts/<model-id>.json`
(normally `~/.config/klighting/layouts/`). The model ID is `keyboard.device` in a
preset JSON file; exported bundles wrap that preset in an outer `keyboard` object.
Element IDs come from assignments in that preset. For an uninitialized LampArray,
enable it and refresh discovery first.

```json
{
  "version": 1,
  "model": "replace-with-the-device-model-id",
  "bodyColor": "#333333",
  "keyColor": "#111111",
  "keys": [
    { "id": 7, "label": "W", "rect": [20, 20, 18, 18] }
  ],
  "strips": [
    { "id": 65536, "width": 4, "path": [[88, 0, 0], [89, 100, 0]] }
  ]
}
```

All fields except `version` and `model` are optional. Replace example IDs with actual
device IDs and omit sections you do not need. Rectangles use `[x, y, width, height]`;
strip points use `[elementId, x, y]` in the same units. A strip path must include all
of its elements. Partial key geometry overrides are allowed. Refresh/reopen settings
after editing. Malformed overrides are ignored as a whole.

Overrides affect drawing and labels only. They cannot add LEDs, change IDs, enable
unsupported capabilities or issue hardware commands. A specialized `.so` driver can
still supply a complete verified design and protocol instead.
