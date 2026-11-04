# Arch Linux / Manjaro package

This PKGBUILD builds `klighting-git` for pacman from upstream `main`.

On a fully upgraded system with `base-devel` installed, run as an ordinary user
from this directory:

```sh
makepkg -si
```

This installs missing dependencies, builds and tests the package, then installs
it through pacman. Use `makepkg -s` to build without installing the result.
Set `CMAKE_BUILD_PARALLEL_LEVEL` to limit compilation jobs if needed.

Log out and back in after installation or updates to load the native plugins.
See [activation and diagnostics](../../docs/install.md) and the relevant
[driver module](../../src/drivers/) for hardware requirements. The package installs
the diagnostic tool as `klighting-probe`; without arguments it lists devices read-only.

The binary package requires the KWin release used to build both native
plugins. When upgrading KWin:

1. Disable devices in Lighting and apply to allow restoration, then run
   `systemctl --user stop klightingd.service`.
2. Remove the package with `sudo pacman -R klighting-git`, then upgrade the system
   with `sudo pacman -Syu`.
3. Rebuild and install from this directory with `makepkg -Cfsi`.
4. Log out and back in, then re-enable the devices in Lighting and apply.
