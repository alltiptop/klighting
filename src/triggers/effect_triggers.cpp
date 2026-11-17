// SPDX-License-Identifier: GPL-3.0-or-later
#include "effect_triggers.h"
#include <cmath>
#include <numbers>

namespace Ambient
{
void EffectTriggers::configure(
    const QList<EffectPreset>& effects, const QSet<QString>& used, const PowerState& power)
{
    QMap<QString, State> next;
    for (const auto& input : effects) {
        const auto p = canonicalEffect(input);
        if (p.type == "ambient" || (p.trigger.type.isEmpty() && p.type != "pulse") || !used.contains(p.id)
            || batteryPolicyActive(power, p.pauseOnBattery, p.pauseOnLowBattery))
            continue;
        auto state = m_states.value(p.id);
        if (!m_states.contains(p.id) || state.effect.trigger != p.trigger)
            state = { p, p.trigger.type.isEmpty(), 0 };
        else
            state.effect = p;
        next.insert(p.id, state);
    }
    m_states = next;
}
QList<EffectPreset> EffectTriggers::requirements() const
{
    QList<EffectPreset> result;
    for (const auto& state : m_states)
        result.append(state.effect);
    return result;
}
bool EffectTriggers::needs(const QString& type) const
{
    for (const auto& state : m_states)
        if (state.effect.trigger.type == type)
            return true;
    return false;
}
bool EffectTriggers::setValue(const QString& id, const QVariant& value)
{
    auto it = m_states.find(id);
    if (it == m_states.end() || eventTrigger(it->effect.trigger))
        return false;
    const bool active = triggerMatches(it->effect.trigger, value);
    if (it->active == active)
        return false;
    it->active = active;
    it->elapsed = 0;
    return true;
}
bool EffectTriggers::event(const QString& id, const QVariant& value)
{
    auto it = m_states.find(id);
    if (it == m_states.end() || it->active || !eventTrigger(it->effect.trigger)
        || !triggerMatches(it->effect.trigger, value))
        return false;
    it->active = true;
    it->elapsed = 0;
    return true;
}
bool EffectTriggers::notify(const NotificationEvent& e)
{
    bool changed = false;
    for (auto it = m_states.begin(); it != m_states.end(); ++it) {
        const auto& t = it->effect.trigger;
        if (notificationMatches(t, e))
            changed |= event(it.key());
    }
    return changed;
}
void EffectTriggers::advance(double ms)
{
    if (!std::isfinite(ms) || ms < 0)
        return;
    for (auto& state : m_states) {
        if (!state.active)
            continue;
        state.elapsed += ms;
        if (eventTrigger(state.effect.trigger) && state.elapsed >= state.effect.trigger.duration)
            state.active = false;
        else if (!eventTrigger(state.effect.trigger) && state.effect.type == "pulse")
            state.elapsed = std::fmod(state.elapsed, state.effect.notification.period);
    }
}
void EffectTriggers::reset()
{
    for (auto& state : m_states) {
        state.active = state.effect.trigger.type.isEmpty();
        state.elapsed = 0;
    }
}
bool EffectTriggers::animating() const
{
    for (const auto& state : m_states)
        if (state.active && state.effect.type == "pulse")
            return true;
    return false;
}
int EffectTriggers::nextUpdateMs() const
{
    int delay = -1;
    for (const auto& state : m_states) {
        if (!state.active)
            continue;
        if (state.effect.type == "pulse")
            return 17;
        if (eventTrigger(state.effect.trigger)) {
            const int remaining = std::max(0, int(std::ceil(state.effect.trigger.duration - state.elapsed)));
            delay = delay < 0 ? remaining : std::min(delay, remaining);
        }
    }
    return delay;
}
QMap<QString, QColor> EffectTriggers::colors() const
{
    QMap<QString, QColor> result;
    for (auto it = m_states.cbegin(); it != m_states.cend(); ++it) {
        if (!it->active || it->effect.type == "brightness")
            continue;
        auto color = it->effect.solidColor;
        if (it->effect.type == "pulse") {
            const auto& p = it->effect.notification;
            const double phase = std::fmod(it->elapsed, p.period) / p.period;
            const double alpha = p.pattern == "blink" ? (phase < .5 ? 1 : 0)
                : p.pattern == "double"               ? (phase < .15 || (phase >= .3 && phase < .45) ? 1 : 0)
                                                      : (1 - std::cos(2 * std::numbers::pi * phase)) / 2;
            color = p.color;
            color.setAlphaF(alpha);
        }
        result.insert(it.key(), color);
    }
    return result;
}
QMap<QString, int> EffectTriggers::brightness() const
{
    QMap<QString, int> result;
    for (auto it = m_states.cbegin(); it != m_states.cend(); ++it)
        if (it->active && it->effect.type == "brightness")
            result.insert(it.key(), it->effect.brightness);
    return result;
}
}
