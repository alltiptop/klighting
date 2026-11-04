// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "device_driver.h"
#include <linux/input-event-codes.h>

namespace TestFixture
{
inline constexpr int KeyCount = 84;
inline Ambient::DeviceDescription keyboardDescription()
{
    using namespace Ambient;
    DeviceDescription d;
    d.id = QStringLiteral("test-keyboard");
    d.name = QStringLiteral("Test keyboard");
    d.bodyColor = QColor("#252932");
    d.keyColor = QColor("#121722");
    d.capabilities = DeviceCapability::Brightness | DeviceCapability::Color | DeviceCapability::PerKeyColor
        | DeviceCapability::Lightbar | DeviceCapability::LightbarPower | DeviceCapability::PerStripColor;
    for (int id = 0; id < KeyCount; ++id)
        d.keys.append({ id, QString::number(id + 1), QRectF((id % 14) * 20, (id / 14) * 18, 18, 16) });
    for (int i = 0; i < 4; ++i)
        d.keys[4 + i].label = QStringLiteral("F%1").arg(9 + i);
    DeviceStrip strip { FirstStripId, QStringLiteral("Test strip"), { } };
    for (int i = 0; i < 12; ++i)
        strip.elements.append({ 1001 + i * 2, QString::number(i + 1), { } });
    d.strips.append(strip);
    d.groups = { { QStringLiteral("Test group"), { 1, 3, 5 } } };
    return d;
}
inline QVariantMap inputKeyCodes()
{
    return { { "12", KEY_W }, { "70", KEY_LEFTSHIFT }, { "71", KEY_RIGHTSHIFT }, { "72", KEY_LEFTCTRL },
        { "73", KEY_LEFTMETA } };
}
}
