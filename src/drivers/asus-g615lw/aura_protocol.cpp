// SPDX-License-Identifier: GPL-3.0-or-later
#include "aura_protocol.h"
#include <QDBusMetaType>

namespace Ambient
{
QDBusArgument& operator<<(QDBusArgument& arg, const AuraColor& v)
{
    arg.beginStructure();
    arg << v.r << v.g << v.b;
    arg.endStructure();
    return arg;
}
const QDBusArgument& operator>>(const QDBusArgument& arg, AuraColor& v)
{
    arg.beginStructure();
    arg >> v.r >> v.g >> v.b;
    arg.endStructure();
    return arg;
}
QDBusArgument& operator<<(QDBusArgument& arg, const AuraEffect& v)
{
    arg.beginStructure();
    arg << v.mode << v.zone << v.first << v.second << v.speed << v.direction;
    arg.endStructure();
    return arg;
}
const QDBusArgument& operator>>(const QDBusArgument& arg, AuraEffect& v)
{
    arg.beginStructure();
    arg >> v.mode >> v.zone >> v.first >> v.second >> v.speed >> v.direction;
    arg.endStructure();
    return arg;
}
QDBusArgument& operator<<(QDBusArgument& arg, const AuraPowerState& v)
{
    arg.beginStructure();
    arg << v.zone << v.boot << v.awake << v.sleep << v.shutdown;
    arg.endStructure();
    return arg;
}
const QDBusArgument& operator>>(const QDBusArgument& arg, AuraPowerState& v)
{
    arg.beginStructure();
    arg >> v.zone >> v.boot >> v.awake >> v.sleep >> v.shutdown;
    arg.endStructure();
    return arg;
}
QDBusArgument& operator<<(QDBusArgument& arg, const AuraPower& v)
{
    arg.beginStructure();
    arg << v.states;
    arg.endStructure();
    return arg;
}
const QDBusArgument& operator>>(const QDBusArgument& arg, AuraPower& v)
{
    arg.beginStructure();
    arg >> v.states;
    arg.endStructure();
    return arg;
}
void registerAuraTypes()
{
    qDBusRegisterMetaType<AuraColor>();
    qDBusRegisterMetaType<AuraEffect>();
    qDBusRegisterMetaType<AuraPowerState>();
    qDBusRegisterMetaType<QList<AuraPowerState>>();
    qDBusRegisterMetaType<AuraPower>();
    qDBusRegisterMetaType<Interfaces>();
    qDBusRegisterMetaType<ManagedObjects>();
    qDBusRegisterMetaType<Packets>();
    qDBusRegisterMetaType<QList<uint>>();
}
std::optional<AuraPowerState> decodeLightbarPower(const QVariant& value)
{
    return decodeZonePower(value, LightbarZone);
}
std::optional<AuraPowerState> decodeZonePower(const QVariant& value, uint zone)
{
    if (value.metaType() != QMetaType::fromType<AuraPower>()
        && (value.metaType() != QMetaType::fromType<QDBusArgument>()
            || value.value<QDBusArgument>().currentSignature() != QLatin1String("(a(ubbbb))")))
        return std::nullopt;
    std::optional<AuraPowerState> lightbar;
    for (const auto& state : qdbus_cast<AuraPower>(value).states) {
        if (state.zone != zone)
            continue;
        if (lightbar)
            return std::nullopt;
        lightbar = state;
    }
    return lightbar;
}
std::optional<AuraEffect> decodeEffect(const QVariant& value)
{
    if (value.metaType() != QMetaType::fromType<AuraEffect>()
        && (value.metaType() != QMetaType::fromType<QDBusArgument>()
            || value.value<QDBusArgument>().currentSignature() != QLatin1String("(uu(yyy)(yyy)ss)")))
        return std::nullopt;
    const auto result = qdbus_cast<AuraEffect>(value);
    return effectPacket(result) ? std::optional(result) : std::nullopt;
}
std::optional<QByteArray> effectPacket(const AuraEffect& effect)
{
    // asusctl 6.5.0 rog-aura/src/builtin_modes.rs: AuraEffect -> HID report.
    // Only the whole-device mode is supported here. Never guess a per-key layout.
    const QList<uint> modes { 0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12 };
    const QMap<QString, uchar> speeds { { "Low", 0xe1 }, { "Med", 0xeb }, { "High", 0xf5 } };
    const QMap<QString, uchar> directions { { "Right", 0 }, { "Left", 1 }, { "Up", 2 }, { "Down", 3 } };
    if (effect.zone != 0 || !modes.contains(effect.mode) || !speeds.contains(effect.speed)
        || !directions.contains(effect.direction))
        return std::nullopt;
    QByteArray packet(64, '\0');
    packet[0] = char(0x5d);
    packet[1] = char(0xb3);
    packet[3] = char(effect.mode);
    packet[4] = char(effect.first.r);
    packet[5] = char(effect.first.g);
    packet[6] = char(effect.first.b);
    packet[7] = char(speeds.value(effect.speed));
    packet[8] = char(directions.value(effect.direction));
    packet[10] = char(effect.second.r);
    packet[11] = char(effect.second.g);
    packet[12] = char(effect.second.b);
    return packet;
}
QByteArray setPacket()
{
    // 19b6 declares a 64-byte output report, including its report ID. asusd
    // 6.5.0's raw path appends a short (17-byte) SET that newer firmware ignores.
    QByteArray packet(64, '\0');
    packet[0] = char(0x5d);
    packet[1] = char(0xb5);
    return packet;
}
bool compatibleAura(const QString& path, const QVariantMap& p)
{
    // The supported 19b6 controller uses whole-device Aura restoration:
    // no zoned/multizone restoration that this implementation cannot preserve.
    if (!path.startsWith(QLatin1String("/xyz/ljones/aura/19b6_")) || !p.contains("DeviceType")
        || p.value("DeviceType").toUInt() != 0 || !p.contains("SupportedBasicZones")
        || !p.contains("Brightness") || !p.contains("LedModeData"))
        return false;
    const auto zones = qdbus_cast<QList<uint>>(p.value("SupportedBasicZones"));
    const auto modes = qdbus_cast<QList<uint>>(p.value("SupportedBasicModes"));
    const auto zoneValue = p.value("SupportedBasicZones");
    const bool validZones = zoneValue.metaType() == QMetaType::fromType<QList<uint>>()
        || (zoneValue.metaType() == QMetaType::fromType<QDBusArgument>()
            && zoneValue.value<QDBusArgument>().currentSignature() == QLatin1String("au"));
    return validZones && zones.isEmpty() && modes.contains(0) && p.value("Brightness").toUInt() <= 3
        && decodeEffect(p.value("LedModeData")).has_value();
}
}
