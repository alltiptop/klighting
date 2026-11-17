// SPDX-License-Identifier: GPL-3.0-or-later
#include "notification_effect.h"

namespace Ambient
{
bool notificationMatches(const NotificationOptions& options, const NotificationEvent& event)
{
    EffectPreset legacy;
    legacy.type = "notifications";
    legacy.notification = options;
    return notificationMatches(canonicalEffect(legacy).trigger, event);
}
}
