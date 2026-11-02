// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "power_state.h"
#include "presets.h"

namespace Ambient
{
// Only structured routing metadata is retained. No notification text or images.
struct NotificationEvent {
    QString application, desktopEntry, category, eventId;
    int urgency = 1;
    QString trigger = QStringLiteral("notification");
};
bool notificationMatches(const NotificationOptions& options, const NotificationEvent& event);
class NotificationEffects
{
public:
    void configure(
        const QList<EffectPreset>& presets, const QSet<QString>& used, const PowerState& power = { });
    void trigger(const NotificationEvent& event);
    void advance(double elapsedMs);
    void reset();
    bool empty() const { return m_states.isEmpty(); }
    bool active() const;
    QMap<QString, QColor> colors() const;

private:
    struct State {
        NotificationOptions options;
        double elapsed = -1;
    };
    QMap<QString, State> m_states;
};
}
