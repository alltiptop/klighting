# ASUS TUF Gaming A17 FA706IHRB

This driver supports the `FA706IHRB` board with one keyboard RGB zone: uniform
colors, Ambient effects and global notification effects. Per-key RGB and lightbars
are unsupported. The keyboard preview is an ANSI schematic with a numeric keypad;
Fn has no input-preview binding.

The TUF driver and generic drivers for the internal keyboard are alternative
controllers: enable only one of them.

## Requirements

A running `asusd` must expose the legacy D-Bus API used by
[asusctl 6.0.11.r10.g5ab9642](https://github.com/OpenGamingCollective/asusctl/tree/5ab9642b790a773f74002b6574dc64eea4de51b1):

- Service: `org.asuslinux.Daemon`
- Object: `/org/asuslinux/tuf`
- Interface: `org.asuslinux.Aura`
- Method: `DirectAddressingRaw(aay)`

The `xyz.ljones.*` API and other TUF board IDs are unsupported. Select the factory
**Static** mode before enabling the driver; other modes are rejected.

## Output and restoration

Output is asynchronous and limited to 20 FPS. Pending colors are coalesced and
unchanged colors are skipped. The backend reconnects when the daemon is replaced.
After resume from suspend, reported by logind's `PrepareForSleep` signal, the
driver writes the current color again.

Disabling the device in Lighting or stopping `klightingd` restores the Static
color saved in `asusd`. Both wait for the restoration, up to 2 s when `asusd` does
not respond. Restoration requires the same `asusd` instance that received the
Lighting colors; a `klightingd` crash leaves the last color. A changed saved color
cancels restoration; while enabled, the driver continues applying Lighting effects.

Output and restoration are suppressed while `asusd` reports brightness Off or
keyboard power off. Legacy `asusd` can raise brightness from Off to Med and save
it if its cached state is stale or brightness/power changes between the state
check and the RGB write.

## Diagnostics

Follow the project [build](../../../README.md#build) and
[installation](../../../docs/install.md) instructions. Read-only discovery should
list `asus-tuf-fa706ihrb:internal`.

Before testing colors, disable the device in Lighting and apply, then stop
`klightingd`. Run in the same user session:

```sh
./build/ambient_probe --test-colors --device asus-tuf-fa706ihrb:internal
```

The command cycles colors for 12 seconds, then allows 3 seconds for restoration.
Check the color transitions and restoration of the saved Static color visually.
Keep brightness and factory mode unchanged during the test. `--test-keys` is
unsupported for this single-zone device.
