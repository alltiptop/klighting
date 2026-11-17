// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "triggers/effect_triggers.h"

namespace Ambient
{
// Compatibility helpers for version-1 notification definitions. New effects use EffectTriggers.
bool notificationMatches(const NotificationOptions& options, const NotificationEvent& event);
class NotificationEffects
{
public:
    void configure(
        const QList<EffectPreset>& presets, const QSet<QString>& used, const PowerState& power = { })
    {
        m_triggers.configure(presets, used, power);
    }
    bool trigger(const NotificationEvent& event) { return m_triggers.notify(event); }
    void advance(double elapsedMs) { m_triggers.advance(elapsedMs); }
    void reset() { m_triggers.reset(); }
    bool empty() const { return m_triggers.requirements().isEmpty(); }
    bool active() const { return m_triggers.nextUpdateMs() >= 0; }
    QMap<QString, QColor> colors() const { return m_triggers.colors(); }

private:
    EffectTriggers m_triggers;
};
}
