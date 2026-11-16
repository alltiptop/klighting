// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "presets/presets.h"
#include "sources/microphone_source.h"
#include "sources/power_state.h"

namespace Ambient
{
QMap<QString, QColor> activeMicrophoneEffects(
    const QList<EffectPreset>& presets, const QSet<QString>& used, const PowerState& power);
QMap<QString, QColor> microphoneColors(const QMap<QString, QColor>& effects, const MicrophoneState& state);
}
