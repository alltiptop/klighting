// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QColor>
#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QVariantMap>
#include <optional>

namespace Ambient
{
inline constexpr auto AuraInterface = "xyz.ljones.Aura";
inline constexpr auto AsusdService = "xyz.ljones.Asusd";
struct AuraColor {
    uchar r = 0, g = 0, b = 0;
};
struct AuraEffect {
    uint mode = 0, zone = 0;
    AuraColor first, second;
    QString speed = QStringLiteral("Med"), direction = QStringLiteral("Right");
};
inline constexpr uint LightbarZone = 2;
struct AuraPowerState {
    uint zone = 0;
    bool boot = false, awake = false, sleep = false, shutdown = false;
};
struct AuraPower {
    QList<AuraPowerState> states;
};
using Interfaces = QMap<QString, QVariantMap>;
using ManagedObjects = QMap<QDBusObjectPath, Interfaces>;
using Packets = QList<QByteArray>;
QDBusArgument& operator<<(QDBusArgument& arg, const AuraColor& value);
const QDBusArgument& operator>>(const QDBusArgument& arg, AuraColor& value);
QDBusArgument& operator<<(QDBusArgument& arg, const AuraEffect& value);
const QDBusArgument& operator>>(const QDBusArgument& arg, AuraEffect& value);
QDBusArgument& operator<<(QDBusArgument& arg, const AuraPowerState& value);
const QDBusArgument& operator>>(const QDBusArgument& arg, AuraPowerState& value);
QDBusArgument& operator<<(QDBusArgument& arg, const AuraPower& value);
const QDBusArgument& operator>>(const QDBusArgument& arg, AuraPower& value);
void registerAuraTypes();
std::optional<AuraPowerState> decodeLightbarPower(const QVariant& value);
std::optional<AuraPowerState> decodeZonePower(const QVariant& value, uint zone);
std::optional<AuraEffect> decodeEffect(const QVariant& value);
std::optional<QByteArray> effectPacket(const AuraEffect& effect);
QByteArray setPacket();
bool compatibleAura(const QString& path, const QVariantMap& properties);
}
Q_DECLARE_METATYPE(Ambient::AuraColor)
Q_DECLARE_METATYPE(Ambient::AuraEffect)
Q_DECLARE_METATYPE(Ambient::AuraPowerState)
Q_DECLARE_METATYPE(Ambient::AuraPower)
Q_DECLARE_METATYPE(Ambient::Interfaces)
Q_DECLARE_METATYPE(Ambient::ManagedObjects)
