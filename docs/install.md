# Installation and diagnostics

## Install

Preview paths, then install with permission to write to the configured prefix:

```sh
DESTDIR="$PWD/stage" cmake --install build
sudo cmake --install build
```

Do not overwrite a library currently loaded in KWin. Log out before replacing a
running installation, or use the atomic [local installer](../tools/install-local.sh)
for a verified artifact bundle. Build it from the configured CMake install rules;
driver libraries and assets are included automatically. Python 3.11+ is required
for the installer. Use only a trusted local build: checksums detect corruption,
not malicious native code.

```sh
cmake --build build --target local_bundle
tools/install-local.sh --check build/local-bundle
sudo tools/install-local.sh build/local-bundle
```

The installer honors the configured installation paths, preserves backups under
`/var/lib/klighting/backups/` and prints activation commands. It does not restart
processes or change user settings. Each driver declares its own legacy plugin aliases.

The installer checks each plugin in a separate process before writing system files.
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
whether Ambient is enabled or not. `klightingd` loads it automatically. After three
startup attempts without stable desktop state, automatic loading pauses across KWin
and service restarts. Use **Retry desktop integration** in Lighting settings, or:

```sh
qdbus6 org.klighting.Service /org/klighting/Service org.klighting.Service1.RetryDesktopIntegration
```

The budget resets after 60 seconds of valid desktop state. Startup state is stored in
`$XDG_STATE_HOME/klighting/desktop-startup.json` (normally `~/.local/state/klighting/`).
Direct KWin loading requires a fresh one-use permit from the service, including when
a previous installation left native autoload enabled. A KWin restart is not itself
evidence that this integration caused a crash.

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

## Application configuration

On startup or Reload, `klightingd` imports the remaining `Effect-ambientled` options
from `kwinrc` into `$XDG_CONFIG_HOME/klighting/application.json` (normally under
`~/.config`). The supported values are `ScreenName`, `Region` (`full` or `bottom`),
`BottomPercent` (1–100), `Brightness` (0–1) and `Saturation` (0–2):

```json
{
  "version": 1,
  "legacyImported": true,
  "values": { "ScreenName": "", "Region": "full", "BottomPercent": 25,
              "Brightness": 1, "Saturation": 1 }
}
```

The service is the only migration writer. Existing new values win; data and the
completion marker are published together under a nonblocking lock. Each application
or driver file migrates independently, so an interrupted partial migration can retry
without overwriting completed files. Invalid files remain untouched and Reload reports
an error while keeping the last valid running configuration. The editor and discovery
only read settings, using legacy values in memory until migration succeeds.

`kwinrc` is not rewritten or removed; component enable flags remain there. Preset/effect
files retain their existing `kwin-effect-ambient-led/` location. Driver-specific options
belong under `klighting/drivers/<driver-id>.json`; their meanings and defaults are
documented inside the relevant [driver module](../src/drivers/).

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

Status includes build IDs, IPC capabilities and provider instance IDs. Lighting settings
compare them with installed plugin metadata and `share/klighting/klighting-service.json`
under the configured installation prefix. Keep that record with its daemon binary.
Missing metadata means unknown; a different build is a restart hint, not an IPC failure.
Compatibility uses the protocol and capabilities. Build IDs fingerprint host source/build
inputs without Git; independent driver modules are excluded. These are diagnostic
identifiers, not artifact authenticity checks.

Desktop status distinguishes window, key and lock availability. Losing one input leaves
the others usable; unknown lock state retains the last policy. A newly connected desktop
with unknown lock state waits before enabling output, while no desktop provider permits
standalone lighting. Capture reports provider, screen, request and readiness separately.
A missing screen holds the last Ambient color and keeps accepting configuration; other
effects continue. GPU failure stops sampling until explicit effect reconfiguration or
reload, without automatically retrying GPU work on every service poll.

Screen capture prevents direct scanout while needed, which can increase GPU use in
fullscreen applications. Fractional scaling, rotation, HDR and multiple monitors
need hardware testing. Ambient currently selects one screen color per effect, not
spatially mapped colors per key. See the relevant [driver module](../src/drivers/)
for hardware access, brightness, restoration and controller conflicts.
