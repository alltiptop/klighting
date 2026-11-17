// SPDX-License-Identifier: GPL-3.0-or-later
#include "trigger_options.h"
#include <QRegularExpression>
#include <cmath>

namespace Ambient
{
QStringList triggerTypes()
{
    return { { }, "microphone", "volume", "muted", "playback", "power-profile", "display", "bluetooth",
        "network", "touchpad", "airplane", "keyboard-brightness", "notification", "power-connected", "dbus" };
}
bool eventTrigger(const TriggerOptions& t)
{
    return t.type == "notification" || t.type == "power-connected" || (t.type == "dbus" && t.signal);
}
bool triggerMatches(const TriggerOptions& t, const QVariant& value)
{
    if (!value.isValid() || value.isNull())
        return false;
    if (t.comparison == "any")
        return true;
    bool equal = false;
    if (t.value.metaType().id() == QMetaType::Bool) {
        if (value.metaType().id() != QMetaType::Bool)
            return false;
        equal = value.toBool() == t.value.toBool();
    } else if (t.value.metaType().id() == QMetaType::QString) {
        if (value.metaType().id() != QMetaType::QString)
            return false;
        equal = value.toString() == t.value.toString();
    } else {
        if (value.metaType().id() == QMetaType::Bool || value.metaType().id() == QMetaType::QString)
            return false;
        bool aOk = false, bOk = false;
        const double a = value.toDouble(&aOk), b = t.value.toDouble(&bOk);
        if (!aOk || !bOk || !std::isfinite(a) || !std::isfinite(b))
            return false;
        if (t.comparison == "gt")
            return a > b;
        if (t.comparison == "ge")
            return a >= b;
        if (t.comparison == "lt")
            return a < b;
        if (t.comparison == "le")
            return a <= b;
        equal = a == b;
    }
    return t.comparison == "eq" ? equal : t.comparison == "ne" && !equal;
}
QJsonObject triggerJson(const TriggerOptions& t)
{
    return { { "type", t.type }, { "comparison", t.comparison },
        { "value", QJsonValue::fromVariant(t.value) }, { "duration", t.duration }, { "urgency", t.urgency },
        { "application", t.application }, { "category", t.category }, { "eventId", t.eventId },
        { "bus", t.bus }, { "service", t.service }, { "path", t.path }, { "interface", t.interface },
        { "member", t.member }, { "signal", t.signal } };
}
std::optional<TriggerOptions> parseTrigger(const QJsonObject& o, QString* error)
{
    TriggerOptions t;
    const auto fail = [&]() -> std::optional<TriggerOptions> {
        if (error)
            *error = QStringLiteral("Invalid trigger type, condition, timing or D-Bus address.");
        return std::nullopt;
    };
    for (const auto* key : { "type", "comparison", "application", "category", "eventId", "bus", "service",
             "path", "interface", "member" })
        if (o.contains(key) && (!o[key].isString() || o[key].toString().size() > 256))
            return fail();
    t.type = o["type"].toString();
    t.comparison = o["comparison"].toString(t.comparison);
    if (o.contains("value"))
        t.value = o["value"].toVariant();
    t.duration = o["duration"].toInt(t.duration);
    t.urgency = o["urgency"].toInt(t.urgency);
    t.application = o["application"].toString();
    t.category = o["category"].toString();
    t.eventId = o["eventId"].toString();
    t.bus = o["bus"].toString(t.bus);
    t.service = o["service"].toString();
    t.path = o["path"].toString();
    t.interface = o["interface"].toString();
    t.member = o["member"].toString();
    t.signal = o["signal"].toBool();
    if (!triggerTypes().contains(t.type)
        || !QStringList { "eq", "ne", "gt", "ge", "lt", "le", "any" }.contains(t.comparison)
        || (o.contains("signal") && !o["signal"].isBool())
        || !QStringList { "session", "system" }.contains(t.bus)
        || (o.contains("duration") && o["duration"].toDouble(-1) != t.duration) || t.duration < 100
        || t.duration > 60000 || (o.contains("urgency") && o["urgency"].toDouble(-9) != t.urgency)
        || t.urgency < -1 || t.urgency > 2)
        return fail();
    const auto v = QJsonValue::fromVariant(t.value);
    if (!(v.isBool() || v.isDouble() || v.isString()) || (v.isDouble() && !std::isfinite(v.toDouble()))
        || (v.isString() && v.toString().size() > 256))
        return fail();
    if (t.type == "dbus") {
        static const QRegularExpression name(
            QStringLiteral("\\A[A-Za-z_][A-Za-z0-9_]*(\\.[A-Za-z_][A-Za-z0-9_]*)+\\z"));
        static const QRegularExpression service(
            QStringLiteral("\\A[A-Za-z_][A-Za-z0-9_-]*(\\.[A-Za-z_][A-Za-z0-9_-]*)+\\z"));
        static const QRegularExpression path(QStringLiteral("\\A/(?:[A-Za-z0-9_]+(?:/[A-Za-z0-9_]+)*)?\\z"));
        static const QRegularExpression member(QStringLiteral("\\A[A-Za-z_][A-Za-z0-9_]*\\z"));
        // Empty fields are permitted while editing; incomplete observers remain inactive.
        if ((!t.service.isEmpty() && !service.match(t.service).hasMatch())
            || (!t.path.isEmpty() && !path.match(t.path).hasMatch())
            || (!t.interface.isEmpty() && !name.match(t.interface).hasMatch())
            || (!t.member.isEmpty() && !member.match(t.member).hasMatch()))
            return fail();
    }
    return t;
}
}
