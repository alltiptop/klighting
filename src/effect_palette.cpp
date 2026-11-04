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
            state.pendingSample.reset();
            state.nextSample = state.nextOutput = 0;
            if (!state.color.isValid()) {
                state.color = adjusted(Qt::black, state.options);
                state.smoother.setTarget(state.color);
                state.smoother.step(0, 0);
            }
        }
        const bool paused = batteryPolicyActive(power, p.pauseOnBattery, p.pauseOnLowBattery);
        if (paused != state.paused) {
            state.nextSample = state.nextOutput = 0;
            state.pendingSample.reset();
        }
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
        state.pendingSample.reset();
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
        if (state.paused)
            continue;
        const auto target = adjusted(screenColor, state.options);
        if (m_time + 0.001 < state.nextSample) {
            // Keep only the newest candidate, including clearing an obsolete one.
            state.pendingSample
                = state.smoother.target() == target ? std::nullopt : std::optional<QColor>(target);
            continue;
        }
        state.pendingSample.reset();
        state.smoother.setTarget(target);
        state.nextSample = m_time + std::ceil(1000.0 / state.options.fps);
    }
}
void EffectPalette::resumeSampling()
{
    // The palette clock stops with the screen source. A recovered source's
    // first frame must not be rejected against a deadline from before the loss.
    for (auto& state : m_states) {
        state.nextSample = m_time;
        state.pendingSample.reset();
    }
}
void EffectPalette::advance(double elapsedMs, bool publish)
{
    elapsedMs = std::isfinite(elapsedMs) ? std::max(elapsedMs, 0.0) : 0;
    const auto before = m_time;
    m_time += elapsedMs;
    for (auto& state : m_states) {
        if (state.paused)
            continue;
        auto remaining = elapsedMs;
        if (state.pendingSample && m_time + 0.001 >= state.nextSample) {
            const auto oldTargetTime = std::clamp(state.nextSample - before, 0.0, elapsedMs);
            state.smoother.step(oldTargetTime, state.options.smoothing);
            state.smoother.setTarget(*state.pendingSample);
            state.pendingSample.reset();
            state.nextSample = m_time + std::ceil(1000.0 / state.options.fps);
            remaining -= oldTargetTime;
        }
        const auto color = state.smoother.step(remaining, state.options.smoothing);
        if (publish && color && m_time + 0.001 >= state.nextOutput) {
            const auto next = constrainBrightness(*color, state.options.minimum, state.options.maximum);
            if (state.smoother.active() || next != state.color) {
                state.color = next;
                state.nextOutput = m_time + ledIntervalMs(state.options.ledFps);
            }
        }
    }
}
int EffectPalette::nextUpdateMs() const
{
    double delay = -1;
    const auto consider = [&](double deadline) {
        const auto remaining = std::max(0.0, deadline - m_time);
        if (delay < 0 || remaining < delay)
            delay = remaining;
    };
    for (const auto& state : m_states) {
        if (state.paused)
            continue;
        if (state.pendingSample)
            consider(state.nextSample);
        const auto current = state.smoother.color();
        if (state.smoother.active()
            || (current
                && constrainBrightness(*current, state.options.minimum, state.options.maximum)
                    != state.color))
            consider(state.nextOutput);
    }
    return delay < 0 ? -1 : int(std::ceil(delay));
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
        const auto assigned = assignments.constFind(id);
        if (assigned == assignments.cend() && style.color.alpha() == 255)
            return;
        const auto list = assigned == assignments.cend() ? implicitEffects(style) : assigned.value();
        for (const auto& effect : list)
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
        const auto assigned = assignments.constFind(id);
        const auto list = assigned == assignments.cend() ? implicitEffects(style) : assigned.value();
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
