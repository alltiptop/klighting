// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "notification_trigger.h"
#include "presets/presets.h"
#include "sources/power_state.h"

namespace Ambient
{
// Evaluates conditions independently of color composition and device assignments.
class EffectTriggers
{
public:
    void configure(const QList<EffectPreset>& effects, const QSet<QString>& used, const PowerState& power);
    QList<EffectPreset> requirements() const;
    bool needs(const QString& type) const;
    bool setValue(const QString& id, const QVariant& value);
    bool event(const QString& id, const QVariant& value = true);
    bool notify(const NotificationEvent& event);
    void advance(double elapsedMs);
    void reset();
    bool animating() const;
    int nextUpdateMs() const;
    QMap<QString, QColor> colors() const;
    QMap<QString, int> brightness() const;

private:
    struct State {
        EffectPreset effect;
        bool active = false;
        double elapsed = 0;
    };
    QMap<QString, State> m_states;
};
}
