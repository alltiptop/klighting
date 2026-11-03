// SPDX-License-Identifier: GPL-3.0-or-later
#include "effect_palette.h"
#include <algorithm>
#include <cmath>

namespace Ambient
{
namespace
{
    QColor adjusted(const QColor& sample, const AmbientOptions& a)
    {
        return adjustColorTemperature(mapBrightness(sample, a.minimum, a.maximum), a.temperature);
    }
}
void EffectPalette::configure(
    const QList<EffectPreset>& presets, const QSet<QString>& used, const PowerState& power)
{
    QMap<QString, State> next;
    m_solids.clear();
    m_brightness.clear();
    for (const auto& p : presets) {
        // Static filters also apply to colors frozen by a device's battery policy.
        if (p.type == "brightness")
            m_brightness.insert(p.id, std::clamp(p.brightness, 0, 100));
        if (p.type == "solid" && used.contains(p.id))
            m_solids.insert(p.id, p.solidColor);
        if (p.type != "ambient" || !used.contains(p.id))
            continue;
        auto state = m_states.value(p.id);
        if (!state.color.isValid() || state.options != p.ambient) {
            state.options = p.ambient;
            state.nextSample = state.nextOutput = 0;
            if (!state.color.isValid()) {
                state.color = adjusted(Qt::black, state.options);
                state.smoother.setTarget(state.color);
                state.smoother.step(0, 0);
            }
        }
        const bool paused = batteryPolicyActive(power, p.pauseOnBattery, p.pauseOnLowBattery);
        if (paused != state.paused)
            state.nextSample = state.nextOutput = 0;
        state.paused = paused;
        next.insert(p.id, state);
    }
    m_states = next;
}
void EffectPalette::reset()
{
    m_time = 0;
    for (auto& state : m_states) {
        if (state.paused)
            continue;
        state.smoother.reset();
        state.nextSample = state.nextOutput = 0;
        state.color = adjusted(Qt::black, state.options);
        state.smoother.setTarget(state.color);
        state.smoother.step(0, 0);
    }
}
void EffectPalette::sample(const QColor& screenColor)
{
    if (!screenColor.isValid())
        return;
    for (auto& state : m_states) {
        if (state.paused || m_time + 0.001 < state.nextSample)
            continue;
        state.smoother.setTarget(adjusted(screenColor, state.options));
        state.nextSample = m_time + std::ceil(1000.0 / state.options.fps);
    }
}
void EffectPalette::advance(double elapsedMs)
{
    elapsedMs = std::isfinite(elapsedMs) ? std::clamp(elapsedMs, 0.0, 1000.0) : 0;
    m_time += elapsedMs;
    for (auto& state : m_states) {
        if (state.paused)
            continue;
        const auto color = state.smoother.step(elapsedMs, state.options.smoothing);
        if (color && m_time + 0.001 >= state.nextOutput) {
            state.color = constrainBrightness(*color, state.options.minimum, state.options.maximum);
            state.nextOutput = m_time + ledIntervalMs(state.options.ledFps);
        }
    }
}
int EffectPalette::samplingFps() const
{
    int result = 0;
    for (const auto& state : m_states)
        if (!state.paused)
            result = std::max(result, state.options.fps);
    return result;
}
int EffectPalette::ledFps() const
{
    int result = 0;
    for (const auto& state : m_states)
        if (!state.paused)
            result = std::max(result, state.options.ledFps);
    return result;
}
QMap<QString, QColor> EffectPalette::colors() const
{
    auto colors = m_solids;
    for (auto it = m_states.cbegin(); it != m_states.cend(); ++it)
        colors.insert(it.key(), it->color);
    return colors;
}
QSet<QString> activeEffects(const DeviceProfile& p, const KeyboardStyles& styles,
    const DeviceDescription& device, const EffectAssignments& assignments)
{
    QSet<QString> ids;
    auto add = [&](int id) {
        const auto style = keyStyle(styles, id);
        if (!assignments.contains(id) && style.color.alpha() == 255)
            return;
        for (const auto& effect : assignments.value(id, implicitEffects(style)))
            if (effect.opacity > 0 && effect.id != QLatin1String(InlineColorId))
                ids.insert(effect.id.isEmpty() ? p.defaultEffectId : effect.id);
    };
    if (device.capabilities.testFlag(DeviceCapability::PerKeyColor)) {
        for (const auto& key : device.keys)
            add(key.id);
    } else if (!device.keys.isEmpty() || device.strips.isEmpty())
        add(device.keys.isEmpty() ? LightbarId : device.keys.first().id);
    if (device.capabilities.testFlag(DeviceCapability::PerStripColor)) {
        for (const auto& strip : device.strips)
            for (const auto& element : strip.elements)
                add(element.id);
    } else if (device.capabilities.testFlag(DeviceCapability::Lightbar))
        add(LightbarId);
    ids.remove(QString());
    return ids;
}
LightingColors lightingColors(const DeviceProfile& p, const KeyboardStyles& styles,
    const DeviceDescription& device, const QMap<QString, QColor>& palette,
    const EffectAssignments& assignments, const QMap<QString, QColor>& pulses,
    const QMap<QString, int>& brightness)
{
    auto colorFor = [&](int id, int fallback) {
        const auto style = keyStyle(styles, styles.contains(id) ? id : fallback);
        QColor color = style.mode == KeyMode::Off ? QColor(Qt::black) : blendColor(Qt::black, style.color);
        bool hasColor = style.mode == KeyMode::Off || style.color.alpha() > 0;
        const auto list = assignments.value(id, implicitEffects(style));
        for (const auto& effect : list) {
            const auto effectId = effect.id.isEmpty() ? p.defaultEffectId : effect.id;
            if (effect.opacity <= 0)
                continue;
            if (brightness.contains(effectId)) {
                // A standalone brightness filter controls a neutral light. Once a
                // color (including explicit black) exists, it only scales that color.
                if (!hasColor)
                    color = Qt::white;
                hasColor = true;
                const double factor = 1.0 - (100 - brightness.value(effectId)) * effect.opacity / 10000.0;
                color = QColor::fromRgbF(
                    color.redF() * factor, color.greenF() * factor, color.blueF() * factor, color.alphaF());
                continue;
            }
            // Color opacity tints Ambient. Transient notifications retain their own color.
            QColor overlay = effect.color.isValid() ? effect.color
                : palette.contains(effectId)        ? blendColor(palette.value(effectId), style.color)
                                                    : pulses.value(effectId, QColor(Qt::transparent));
            overlay.setAlpha((overlay.alpha() * effect.opacity + 50) / 100);
            hasColor |= overlay.alpha() > 0;
            color = blendColor(color, overlay);
        }
        return color;
    };
    LightingColors result { colorFor(device.keys.isEmpty() ? LightbarId : device.keys.first().id, LightbarId),
        { }, { } };
    if (device.capabilities.testFlag(DeviceCapability::PerKeyColor)) {
        for (const auto& key : device.keys)
            result.keys.append(colorFor(key.id, key.id));
        if (!result.keys.isEmpty())
            result.uniform = result.keys.first();
    }
    if (device.capabilities.testFlag(DeviceCapability::Lightbar))
        result.uniform = colorFor(LightbarId, LightbarId);
    if (device.capabilities.testFlag(DeviceCapability::PerStripColor)) {
        for (const auto& strip : device.strips) {
            QList<QColor> colors;
            for (const auto& element : strip.elements)
                colors.append(colorFor(element.id, strip.id));
            if (!std::all_of(
                    colors.begin(), colors.end(), [&](const auto& c) { return c == result.uniform; }))
                result.strips.insert(strip.id, colors);
        }
    }
    if (std::all_of(result.keys.begin(), result.keys.end(),
            [&](const auto& color) { return color == result.uniform; }))
        result.keys.clear();
    return result;
}
}
