#!/usr/bin/env bash
# Atomically install tested artifacts under stable plugin names.
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: sudo $0 dist/kwin-6.7.5" >&2
    exit 2
fi
if [[ $EUID -ne 0 ]]; then
    echo "This installer writes to /usr. Run it yourself with sudo." >&2
    exit 1
fi
source_dir=$(realpath -- "$1")
plugins=/usr/lib/qt6/plugins/kwin/effects/plugins
configs=/usr/lib/qt6/plugins/plasma/kcms/systemsettings
legacy_configs=/usr/lib/qt6/plugins/kwin/effects/configs
drivers=/usr/lib/qt6/plugins/ambient-led/drivers/v4
legacy_drivers=(/usr/lib/qt6/plugins/ambient-led/drivers/v1 /usr/lib/qt6/plugins/ambient-led/drivers/v2 /usr/lib/qt6/plugins/ambient-led/drivers/v3)
apps=/usr/share/applications
categories=/usr/share/systemsettings/categories
target="$plugins/ambientled.so"
driver_target="$drivers/asus_g615lw.so"
config_target="$configs/kcm_ambientled.so"
files=(ambientled.so asus_g615lw.so kcm_ambientled.so kcm_ambientled.desktop org.ambientled.Lighting.desktop settings-ambientled.desktop)
targets=("$target" "$driver_target" "$config_target" "$apps/kcm_ambientled.desktop" "$apps/org.ambientled.Lighting.desktop" "$categories/settings-ambientled.desktop")
for section in devices presets effects applications; do
    files+=("kcm_ambientled_$section.so" "kcm_ambientled_$section.desktop")
    targets+=("$configs/kcm_ambientled_$section.so" "$apps/kcm_ambientled_$section.desktop")
done
for file in "${files[@]}" SHA256SUMS; do
    [[ -f "$source_dir/$file" ]] || { echo "Missing artifact: $source_dir/$file" >&2; exit 1; }
done
[[ -d "$plugins" && -d "$configs" && -d "$apps" && -d "$categories" ]] || { echo "Unexpected KDE plugin layout" >&2; exit 1; }
(cd "$source_dir" && sha256sum --check SHA256SUMS)
if [[ -e "$target" ]]; then
    identical=true
    for i in "${!files[@]}"; do
        cmp -s -- "$source_dir/${files[$i]}" "${targets[$i]}" || identical=false
    done
    if [[ "$identical" == true ]]; then
        echo "This build is already installed. No files changed."
        exit 0
    fi
fi
backup_root=/var/lib/kwin-effect-ambient-led/backups
install -d -m 755 -- "$backup_root" "$drivers"
backup=$(mktemp -d "$backup_root/install.XXXXXXXX")
echo "Backup: $backup"
for destination in "${targets[@]}"; do
    if [[ -f "$destination" ]]; then
        cp -p -- "$destination" "$backup/"
    fi
done
staged=()
cleanup() { for temporary in "${staged[@]}"; do rm -f -- "$temporary"; done; }
trap cleanup EXIT
# Stage complete files first; running processes retain their original mapped inodes.
for i in "${!files[@]}"; do
    temporary=$(mktemp "${targets[$i]%/*}/.ambientled-install.XXXXXXXX")
    staged+=("$temporary")
    mode=644
    [[ "${files[$i]}" == *.so ]] && mode=755
    install -m "$mode" -- "$source_dir/${files[$i]}" "$temporary"
done
for i in "${!files[@]}"; do
    mv -f -- "${staged[$i]}" "${targets[$i]}"
done
for old in "$plugins"/ambientled*.so; do
    name=${old##*/}
    [[ "$old" != "$target" && "$name" =~ ^ambientled(_dev[0-9]+)?\.so$ ]] || continue
    mv -- "$old" "$backup/$name"
done
for old in "$drivers"/asus_g615lw*.so; do
    name=${old##*/}
    [[ "$old" != "$driver_target" && "$name" =~ ^asus_g615lw(_dev[0-9]+)?\.so$ ]] || continue
    mv -- "$old" "$backup/$name"
done
for directory in "${legacy_drivers[@]}"; do
    for old in "$directory"/asus_g615lw*.so; do
        name=${old##*/}
        [[ -f "$old" && "$name" =~ ^asus_g615lw(_dev[0-9]+)?\.so$ ]] || continue
        install -d -m 755 -- "$backup/drivers-${directory##*/}"
        mv -- "$old" "$backup/drivers-${directory##*/}/$name"
    done
done
if [[ -f "$legacy_configs/kwin_ambientled_config.so" ]]; then
    mv -- "$legacy_configs/kwin_ambientled_config.so" "$backup/kwin_ambientled_config.so"
fi
echo "Installed ambientled.so, its driver and the Lighting settings module."
echo "Older plugins were moved to the backup; asusd and user settings were not changed."
echo "Close and reopen System Settings. Lighting is under Appearance & Style, with Devices, Presets, Effects and Applications sections."
echo "Standalone settings: kcmshell6 kcm_ambientled"
echo "The running effect has not been reloaded. Run these commands in your user terminal, without sudo:"
echo "  qdbus6 org.kde.KWin /Effects org.freedesktop.DBus.Properties.Get org.kde.kwin.Effects loadedEffects"
echo "If an ambientled_devN effect is listed, unload that exact name first:"
echo "  qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.unloadEffect ambientled_devN"
echo "Reload the stable effect:"
echo "  qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.unloadEffect ambientled"
echo "  qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadEffect ambientled"
echo "D-Bus loading is temporary. Use Enable Ambient LED effect and Apply to save the startup setting."
echo "Qt may retain the old library after a reload. Log out and back in to ensure updated native code is used."
