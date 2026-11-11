// SPDX-License-Identifier: GPL-3.0-or-later
#include "microphone_effect.h"

namespace Ambient
{
QMap<QString, QColor> activeMicrophoneEffects(
    const QList<EffectPreset>& presets, const QSet<QString>& used, const PowerState& power)
{
    QMap<QString, QColor> result;
    for (const auto& preset : presets) {
        if (preset.type != "microphone" || !used.contains(preset.id)
            || batteryPolicyActive(power, preset.pauseOnBattery, preset.pauseOnLowBattery))
            continue;
        auto color = preset.microphone.color;
        color.setAlpha(255);
        result.insert(preset.id, color);
    }
    return result;
}
QMap<QString, QColor> microphoneColors(const QMap<QString, QColor>& effects, const MicrophoneState& state)
{
    return state.available && state.muteKnown && !state.muted ? effects : QMap<QString, QColor>();
}
}
