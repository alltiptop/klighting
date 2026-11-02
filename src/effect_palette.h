// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "color.h"
#include "power_state.h"
#include "presets.h"

namespace Ambient
{
// Ambient presets share one small screen readback. Each retains its own sample
// cadence, smoothing, brightness, white balance and output cadence.
class EffectPalette
{
public:
    void configure(
        const QList<EffectPreset>& presets, const QSet<QString>& used, const PowerState& power = { });
    void reset();
    void sample(const QColor& screenColor);
    void advance(double elapsedMs);
    int samplingFps() const;
    int ledFps() const;
    QMap<QString, QColor> colors() const;

private:
    struct State {
        AmbientOptions options;
        ColorSmoother smoother;
        QColor color;
        bool paused = false;
        double nextSample = 0, nextOutput = 0;
    };
    QMap<QString, State> m_states;
    QMap<QString, QColor> m_solids;
    double m_time = 0;
};
QSet<QString> activeEffects(const LightingPreset& preset, const KeyboardStyles& styles,
    const DeviceDescription& device, const EffectAssignments& assignments = { });
using LightingColors = DeviceFrame;
LightingColors lightingColors(const LightingPreset& preset, const KeyboardStyles& styles,
    const DeviceDescription& device, const QMap<QString, QColor>& palette,
    const EffectAssignments& assignments = { }, const QMap<QString, QColor>& pulses = { });
}
