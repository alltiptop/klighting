// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "device_driver.h"
#include <array>

namespace Ambient
{
inline constexpr int KeyboardLampCount = 88;
inline constexpr std::array<int, 12> StripLampOrder { 89, 91, 93, 95, 97, 99, 98, 96, 94, 92, 90, 88 };
bool matchesStripLamp(int id, quint32 x, quint32 y);
struct KeyDefinition {
    const char* label;
    int x, y, width, height; // Millimetres; centers reported by the controller.
    unsigned char binding;
    quint32 inputCode = 0; // Linux evdev code; zero when not reported or not known.
};
const std::array<KeyDefinition, KeyboardLampCount>& keyboardLayout();
bool matchesKeyboardLamp(int id, quint32 x, quint32 y, uchar binding);
DeviceDescription g615lwDescription();
QVariantMap g615lwInputKeyCodes();
}
