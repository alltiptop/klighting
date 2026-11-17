// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <QVariant>
#include <optional>

namespace Ambient
{
struct TriggerOptions {
    QString type; // Empty means unconditional.
    QString comparison = QStringLiteral("eq");
    QVariant value = true;
    int duration = 3000; // Lifetime of a one-shot event, in milliseconds.
    int urgency = -1;
    QString application, category, eventId;
    QString bus = QStringLiteral("session"), service, path, interface, member;
    bool signal = false;
    bool operator==(const TriggerOptions&) const = default;
};
QStringList triggerTypes();
bool eventTrigger(const TriggerOptions& options);
bool triggerMatches(const TriggerOptions& options, const QVariant& value);
QJsonObject triggerJson(const TriggerOptions& options);
std::optional<TriggerOptions> parseTrigger(const QJsonObject& json, QString* error);
}
