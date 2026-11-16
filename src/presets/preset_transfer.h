// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "presets.h"

namespace Ambient
{
struct ProfileTransfer {
    DeviceProfile profile;
    QStringList warnings;
};
// Pure conversion: never writes a device or changes shared effect definitions.
ProfileTransfer transferProfile(const DeviceProfile& source, const DeviceInstance& from,
    const DeviceInstance& to, const QList<EffectPreset>& effects);
}
