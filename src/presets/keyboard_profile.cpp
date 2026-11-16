// SPDX-License-Identifier: GPL-3.0-or-later
#include "keyboard_profile.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <algorithm>

namespace Ambient
{
KeyStyle keyStyle(const KeyboardStyles& styles, int id, bool inherit)
{
    return styles.value(id, { inherit ? KeyMode::Inherit : KeyMode::Ambient, Qt::transparent });
}
KeyboardStyles inheritedKeyStyles() { return { }; }
KeyboardStyles parseKeyStyles(const QStringList& entries, bool inherit, int maximumId)
{
    KeyboardStyles styles = inherit ? inheritedKeyStyles() : KeyboardStyles();
    for (const auto& entry : entries) {
        const auto parts = entry.split(QLatin1Char(':'));
        if (parts.size() != 3 && parts.size() != 4)
            continue;
        bool ok = false;
        const int id = parts[0].toInt(&ok);
        if (!ok || id < 0 || id > maximumId)
            continue;
        if (parts[1] == QLatin1String("off"))
            styles[id] = { KeyMode::Off, Qt::transparent };
        else if (parts[1] == QLatin1String("ambient")) {
            const QColor overlay = parts[2].isEmpty() ? QColor(Qt::transparent) : QColor(parts[2]);
            if (overlay.isValid())
                styles[id] = { KeyMode::Ambient, overlay, parts.size() == 4 ? parts[3] : QString() };
        } else if (parts[1] == QLatin1String("static")) {
            const QColor color(parts[2]);
            if ((parts[2].size() == 7 || parts[2].size() == 9) && parts[2].startsWith(QLatin1Char('#'))
                && color.isValid())
                styles[id] = { KeyMode::Static, color };
        }
    }
    return styles;
}
QStringList serializeKeyStyles(const KeyboardStyles& styles, bool inherit)
{
    QStringList entries;
    Q_UNUSED(inherit)
    for (auto it = styles.cbegin(); it != styles.cend(); ++it) {
        const int id = it.key();
        const auto& style = it.value();
        if (style.mode == KeyMode::Static && style.color.isValid())
            entries.append(QStringLiteral("%1:static:%2").arg(id).arg(style.color.name(QColor::HexArgb)));
        else if (style.mode == KeyMode::Off)
            entries.append(QStringLiteral("%1:off:").arg(id));
        else if (style.mode == KeyMode::Ambient)
            entries.append(QStringLiteral("%1:ambient:%2:%3")
                    .arg(id)
                    .arg(style.color.name(QColor::HexArgb), style.effectId));
    }
    return entries;
}
KeyboardStyles effectiveKeyStyles(const KeyboardStyles& base, const KeyboardStyles& shift, bool shiftHeld)
{
    auto styles = base;
    if (shiftHeld)
        for (auto it = shift.cbegin(); it != shift.cend(); ++it)
            if (it.value().mode != KeyMode::Inherit)
                styles[it.key()] = it.value();
    return styles;
}
KeyGroups parseKeyGroups(const QString& json, bool* valid)
{
    KeyGroups groups;
    if (valid)
        *valid = false;
    if (json.size() > 1024 * 1024)
        return groups;
    const auto document = QJsonDocument::fromJson(json.toUtf8());
    const auto object = document.object();
    bool complete = document.isObject() && object.size() <= 32;
    for (auto it = object.begin(); it != object.end() && groups.size() < 32; ++it) {
        const QString name = it.key().trimmed();
        if (name.isEmpty() || name.size() > 64 || name != it.key() || !it.value().isArray()) {
            complete = false;
            continue;
        }
        QList<int> keys;
        QSet<int> seen;
        for (const auto& value : it.value().toArray()) {
            const int id = value.toInt(-1);
            if (value.isDouble() && value.toDouble() == id && id >= 0 && id <= MaxAssignmentId
                && !seen.contains(id)) {
                keys.append(id);
                seen.insert(id);
            } else
                complete = false;
        }
        std::sort(keys.begin(), keys.end());
        if (!keys.isEmpty())
            groups.insert(name, keys);
        else
            complete = false;
    }
    if (valid)
        *valid = complete;
    return groups;
}
QString serializeKeyGroups(const KeyGroups& groups)
{
    QJsonObject object;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        QJsonArray keys;
        for (int id : it.value())
            keys.append(id);
        object.insert(it.key(), keys);
    }
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}
QColor blendColor(const QColor& background, const QColor& overlay)
{
    const int alpha = overlay.alpha();
    const auto blend
        = [alpha](int below, int above) { return (below * (255 - alpha) + above * alpha + 127) / 255; };
    return QColor(blend(background.red(), overlay.red()), blend(background.green(), overlay.green()),
        blend(background.blue(), overlay.blue()));
}
QColor assignmentColor(
    const KeyStyle& style, const QString& defaultEffect, const QMap<QString, QColor>& effects)
{
    if (style.mode == KeyMode::Off)
        return Qt::black;
    const QColor background = style.mode == KeyMode::Static
        ? QColor(Qt::black)
        : effects.value(style.effectId.isEmpty() ? defaultEffect : style.effectId, QColor(Qt::black));
    return blendColor(background, style.color);
}
}
