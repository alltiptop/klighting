# Installation and diagnostics

## Install

Preview paths, then install with permission to write to the configured prefix:

```sh
DESTDIR="$PWD/stage" cmake --install build
sudo cmake --install build
```

Do not overwrite a library currently loaded in KWin. Log out before replacing a
running installation, or use the atomic [local installer](../tools/install-local.sh)
for a verified artifact bundle on its supported `/usr/lib/qt6` layout. Include the
built `klightingd`, `klighting-plugin-check`, `klighting_integration.so`, generated D-Bus/systemd/autostart
files and existing effect/driver/KCM artifacts in that bundle's `SHA256SUMS`.
The installer preserves backups and prints activation commands; it does not restart
processes or change user settings itself.

Check a bundle without installing it:

```sh
tools/install-local.sh --check dist/kwin-6.7.5
```

The installer checks each plugin in a separate process before writing to `/usr`.
When invoked with sudo, checks run as the invoking user, never as root. A missing
dependency, crash or timeout rejects the bundle. For a normal CMake installation,
run `ctest --test-dir build -R plugin_loading_tests --output-on-failure` first.
These checks do not exercise rendering or hardware.

Reopen **Lighting** and **Apply** to start/reload `klightingd`. The service starts
automatically at graphical login. It owns colors, presets, notifications and power
policy. **Enable Ambient screen capture** separately enables the optional KWin
screen source; KDE's Desktop Effects checkbox controls the same source. The effect
ID `ambientled`, menu **Lighting** and old configuration path remain stable.

## Reload without logging out

Run in the graphical session's user terminal, without sudo:

```sh
systemctl --user daemon-reload
systemctl --user restart klightingd.service
klightingd --status
```

After rebuilding, install the updated `klightingd` as well as the settings modules,
then restart it. Apply rereads configuration; it does not replace a running binary.
An unsupported preset-version error can indicate an older installed service.

This reloads the lighting core and drivers. To reload Ambient capture:

```sh
qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.unloadEffect ambientled
qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadEffect ambientled
```

The separate desktop integration supplies active windows, modifiers and lock state
whether Ambient is enabled or not. It loads by default; to load it after installation
into an already-running session:

```sh
qdbus6 org.kde.KWin /Plugins org.kde.KWin.Plugins.LoadPlugin klighting_integration
```

For an older installation, list loaded effects and unload each exact old
`ambientled_devN` alias as well:

```sh
qdbus6 org.kde.KWin /Effects org.freedesktop.DBus.Properties.Get org.kde.kwin.Effects loadedEffects
```

The service pauses hardware output while a legacy Ambient module still owns it.
Qt may retain old native code after effect reload. Logging out and back in ensures
updated KWin plugins are loaded; this does not apply to restarting `klightingd`.
Rebuild native KWin plugins after KWin updates. D-Bus loading does not save the
Ambient startup flag; use its checkbox and Apply for that.

## Diagnostics and limitations

```sh
klightingd --status
qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.debug ambientled ""
journalctl --user -b -u klightingd.service
./build/ambient_probe
```

`klightingd --status` does not start the service. To run a development build in the
foreground after stopping an installed service, use `./build/klightingd`; Ctrl+C
releases devices. The core-only build (`-DBUILD_KWIN_EFFECT=OFF`) also builds this
executable. Without KWin, device defaults work; automatic application bindings,
modifier layers and lock tracking need a desktop provider. Only KWin is implemented
currently. Notification input currently requires Plasma's watcher API. Qt and KF6
Config libraries are still required, independently of the desktop session.

Service status reports devices, output and input sources; effect debug reports only
screen capture. A completed USB transfer does not prove physical light output.
Remove private application names and device identities before sharing logs.

Screen capture prevents direct scanout while needed, which can increase GPU use in
fullscreen applications. Fractional scaling, rotation, HDR and multiple monitors
need hardware testing. Ambient currently selects one screen color per effect, not
spatially mapped colors per key. See [G615LW driver notes](../src/drivers/asus-g615lw/README.md)
for USB access, brightness, restoration and controller conflicts.
