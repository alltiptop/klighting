# Installation and diagnostics

## Install

Preview the paths, then install with permission to write to the configured prefix:

```sh
DESTDIR="$PWD/stage" cmake --install build
sudo cmake --install build
```

Do not overwrite a library currently loaded in KWin. Log out before replacing a
running installation, or use the atomic [local installer](../tools/install-local.sh)
for an existing verified artifact bundle on its supported `/usr/lib/qt6` layout.
That installer preserves backups and prints reload commands; it does not reload
KWin or change user settings itself.

Reopen **Lighting**, check **Enable Ambient LED effect**, and **Apply**. This saves
the startup flag and loads the effect. KDE's Desktop Effects checkbox also remains
available. The installed effect ID is `ambientled`; the settings menu stays **Lighting**.
The old `kwin-effect-ambient-led` configuration path is retained.

Temporary reload commands, in the graphical session's user terminal:

```sh
qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.unloadEffect ambientled
qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadEffect ambientled
```

For older development installations, list loaded effects and unload the exact old
`ambientled_devN` alias before loading `ambientled`:

```sh
qdbus6 org.kde.KWin /Effects org.freedesktop.DBus.Properties.Get org.kde.kwin.Effects loadedEffects
```

Reloading does not save the enabled flag or guarantee Qt has released an old native
library. Logging out and back in ensures the new code is loaded. Native KWin ABI
compatibility is not stable: rebuild after KWin updates.

## Diagnostics and limitations

```sh
qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.debug ambientled ""
journalctl --user -b -g 'ambientled'
./build/ambient_probe
```

Debug output reports capture/sample/frame counters, selected preset, device status,
locks and GPU failures. A completed USB transfer does not prove physical light output.
Remove private application names and device identities before sharing logs.

Screen capture prevents direct scanout while needed, which can increase GPU use in
fullscreen applications. Fractional scaling, rotation, HDR and multiple monitors
need hardware testing. Ambient currently selects one screen color per effect, not
spatially mapped colors per key. See [G615LW driver notes](../src/drivers/asus-g615lw/README.md)
for USB access, brightness, restoration and controller conflicts.

