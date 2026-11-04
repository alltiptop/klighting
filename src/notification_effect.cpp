// SPDX-License-Identifier: GPL-3.0-or-later
#include "notification_effect.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace Ambient
{
bool notificationMatches(const NotificationOptions& n, const NotificationEvent& e)
{
    if (n.trigger != e.trigger)
        return false;
    if (n.trigger == "power-connected")
        return true;
    const auto same
        = [](const QString& a, const QString& b) { return a.trimmed().compare(b, Qt::CaseInsensitive) == 0; };
    return (n.application.trimmed().isEmpty() || same(n.application, e.application)
               || same(n.application, e.desktopEntry))
        && (n.urgency < 0 || n.urgency == e.urgency)
        && (n.category.trimmed().isEmpty() || same(n.category, e.category))
        && (n.eventId.trimmed().isEmpty() || same(n.eventId, e.eventId));
}
void NotificationEffects::configure(
    const QList<EffectPreset>& presets, const QSet<QString>& used, const PowerState& power)
{
    QMap<QString, State> next;
    for (const auto& p : presets) {
        if (p.type != "notifications" || !used.contains(p.id)
            || batteryPolicyActive(power, p.pauseOnBattery, p.pauseOnLowBattery))
            continue;
        auto state = m_states.value(p.id);
        if (state.options != p.notification)
            state = { p.notification, -1 };
        next.insert(p.id, state);
    }
    m_states = next;
}
bool NotificationEffects::trigger(const NotificationEvent& event)
{
    // Coalesce bursts while a pattern is running; no unbounded queues, restarts
    // or indefinitely extended flashes from progress notifications.
    bool started = false;
    for (auto& state : m_states)
        if (state.elapsed < 0 && notificationMatches(state.options, event)) {
            state.elapsed = 0;
            started = true;
        }
    return started;
}
void NotificationEffects::advance(double elapsedMs)
{
    if (!std::isfinite(elapsedMs) || elapsedMs < 0)
        return;
    for (auto& state : m_states) {
        if (state.elapsed < 0)
            continue;
        state.elapsed += elapsedMs;
        if (state.elapsed >= state.options.period * state.options.cycles)
            state.elapsed = -1;
    }
}
void NotificationEffects::reset()
{
    for (auto& state : m_states)
        state.elapsed = -1;
}
bool NotificationEffects::active() const
{
    return std::any_of(
        m_states.cbegin(), m_states.cend(), [](const auto& state) { return state.elapsed >= 0; });
}
QMap<QString, QColor> NotificationEffects::colors() const
{
    QMap<QString, QColor> result;
    for (auto it = m_states.cbegin(); it != m_states.cend(); ++it) {
        if (it->elapsed < 0)
            continue;
        const auto& n = it->options;
        const double phase = std::fmod(it->elapsed, n.period) / n.period;
        double alpha;
        if (n.pattern == "blink")
            alpha = phase < 0.5 ? 1 : 0;
        else if (n.pattern == "double")
            alpha = phase < 0.15 || (phase >= 0.3 && phase < 0.45) ? 1 : 0;
        else
            alpha = (1 - std::cos(2 * std::numbers::pi * phase)) / 2;
        auto color = n.color;
        color.setAlphaF(alpha);
        result.insert(it.key(), color);
    }
    return result;
}
}
