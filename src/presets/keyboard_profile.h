// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "device_driver.h"
#include <QColor>
#include <QList>
#include <QMap>
#include <QStringList>

namespace Ambient
{
inline constexpr int LightbarId = FirstStripId; // Logical zone, never a hardware key ID.
enum class KeyMode { Ambient, Static, Off, Inherit };
struct KeyStyle {
    KeyMode mode = KeyMode::Ambient;
    QColor color = Qt::transparent; // Color overlay; alpha blends over the selected effect.
    QString effectId = { }; // Empty uses the keyboard preset's default effect.
    bool operator==(const KeyStyle&) const = default;
};
// Sparse stable IDs: missing Base keys use Ambient, missing layer keys inherit.
// Read through keyStyle() when the layer's default matters.
using KeyboardStyles = QMap<int, KeyStyle>;
KeyStyle keyStyle(const KeyboardStyles& styles, int id, bool inherit = false);
using KeyGroups = QMap<QString, QList<int>>;
KeyboardStyles inheritedKeyStyles();
KeyboardStyles parseKeyStyles(
    const QStringList& entries, bool inherit = false, int maximumId = MaxAssignmentId);
QStringList serializeKeyStyles(const KeyboardStyles& styles, bool inherit = false);
KeyboardStyles effectiveKeyStyles(const KeyboardStyles& base, const KeyboardStyles& shift, bool shiftHeld);
KeyGroups parseKeyGroups(const QString& json, bool* valid = nullptr);
QString serializeKeyGroups(const KeyGroups& groups);
QColor blendColor(const QColor& background, const QColor& overlay);
QColor assignmentColor(
    const KeyStyle& style, const QString& defaultEffect, const QMap<QString, QColor>& effects);
}
