// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "trigger_options.h"

namespace Ambient
{
// Only routing metadata is retained. Notification text and images are never evaluated.
struct NotificationEvent {
    QString application, desktopEntry, category, eventId;
    int urgency = 1;
    QString trigger = QStringLiteral("notification");
};
bool notificationMatches(const TriggerOptions& options, const NotificationEvent& event);
}
