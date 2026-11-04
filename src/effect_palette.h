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
    void resumeSampling();
    // Input events advance time without publishing between LED deadlines.
    void advance(double elapsedMs, bool publish = true);
    int nextUpdateMs() const; // -1 when no transition, pending sample or final output remains.
    int samplingFps() const;
    int ledFps() const;
    QMap<QString, QColor> colors() const;
    const QMap<QString, int>& brightnessFilters() const { return m_brightness; }

private:
    struct State {
        AmbientOptions options;
        ColorSmoother smoother;
        QColor color;
        std::optional<QColor> pendingSample;
        bool paused = false;
        double nextSample = 0, nextOutput = 0;
    };
    QMap<QString, State> m_states;
    QMap<QString, QColor> m_solids;
    QMap<QString, int> m_brightness;
    double m_time = 0;
};
QSet<QString> activeEffects(const DeviceProfile& preset, const KeyboardStyles& styles,
    const DeviceDescription& device, const EffectAssignments& assignments = { });
using LightingColors = DeviceFrame;
LightingColors lightingColors(const DeviceProfile& preset, const KeyboardStyles& styles,
    const DeviceDescription& device, const QMap<QString, QColor>& palette,
    const EffectAssignments& assignments = { }, const QMap<QString, QColor>& pulses = { },
    const QMap<QString, int>& brightness = { });
}
