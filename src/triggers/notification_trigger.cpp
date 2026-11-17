// SPDX-License-Identifier: GPL-3.0-or-later
#include "notification_trigger.h"

namespace Ambient
{
bool notificationMatches(const TriggerOptions& t, const NotificationEvent& e)
{
    if (t.type != e.trigger)
        return false;
    if (t.type == "power-connected")
        return true;
    if (t.type != "notification")
        return false;
    const auto same
        = [](const QString& a, const QString& b) { return a.trimmed().compare(b, Qt::CaseInsensitive) == 0; };
    return (t.application.trimmed().isEmpty() || same(t.application, e.application)
               || same(t.application, e.desktopEntry))
        && (t.urgency < 0 || t.urgency == e.urgency)
        && (t.category.trimmed().isEmpty() || same(t.category, e.category))
        && (t.eventId.trimmed().isEmpty() || same(t.eventId, e.eventId));
}
}
