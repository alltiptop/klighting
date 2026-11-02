// SPDX-License-Identifier: GPL-3.0-or-later
#include "g615lw_layout.h"
#include <QCoreApplication>
#include <linux/input-event-codes.h>

namespace Ambient
{
// G615LW 0b05:19b6: positions and bindings read from LampAttributesResponse.
// Labels and key widths follow ASUS's G615 ANSI product photograph. The five
// macro lamps, Fn and media keys have binding 0, but remain programmable.
const std::array<KeyDefinition, KeyboardLampCount>& keyboardLayout()
{
    static const std::array<KeyDefinition, KeyboardLampCount> keys { {
        { "M1", 75, 43, 17, 9, 0x00 }, // 0
        { "M2", 94, 43, 17, 9, 0x00 }, // 1
        { "M3", 113, 43, 17, 9, 0x00 }, // 2
        { "M4", 132, 43, 17, 9, 0x00 }, // 3
        { "M5", 151, 43, 17, 9, 0x00 }, // 4
        { "Esc", 36, 60, 17, 11, 0x29, KEY_ESC }, // 5
        { "F1", 75, 60, 17, 11, 0x3a, KEY_F1 }, // 6
        { "F2", 94, 60, 17, 11, 0x3b, KEY_F2 }, // 7
        { "F3", 113, 60, 17, 11, 0x3c, KEY_F3 }, // 8
        { "F4", 132, 60, 17, 11, 0x3d, KEY_F4 }, // 9
        { "F5", 157, 60, 17, 11, 0x3e, KEY_F5 }, // 10
        { "F6", 176, 60, 17, 11, 0x3f, KEY_F6 }, // 11
        { "F7", 195, 60, 17, 11, 0x40, KEY_F7 }, // 12
        { "F8", 214, 60, 17, 11, 0x41, KEY_F8 }, // 13
        { "F9", 241, 60, 17, 11, 0x42, KEY_F9 }, // 14
        { "F10", 260, 60, 17, 11, 0x43, KEY_F10 }, // 15
        { "F11", 279, 60, 17, 11, 0x44, KEY_F11 }, // 16
        { "F12", 298, 60, 17, 11, 0x45, KEY_F12 }, // 17
        { "Del", 318, 60, 17, 11, 0x4c, KEY_DELETE }, // 18
        { "`", 36, 76, 17, 17, 0x35, KEY_GRAVE }, // 19
        { "1", 56, 76, 17, 17, 0x1e, KEY_1 }, // 20
        { "2", 75, 76, 17, 17, 0x1f, KEY_2 }, // 21
        { "3", 94, 76, 17, 17, 0x20, KEY_3 }, // 22
        { "4", 113, 76, 17, 17, 0x21, KEY_4 }, // 23
        { "5", 132, 76, 17, 17, 0x22, KEY_5 }, // 24
        { "6", 151, 76, 17, 17, 0x23, KEY_6 }, // 25
        { "7", 170, 76, 17, 17, 0x24, KEY_7 }, // 26
        { "8", 189, 76, 17, 17, 0x25, KEY_8 }, // 27
        { "9", 208, 76, 17, 17, 0x26, KEY_9 }, // 28
        { "0", 227, 76, 17, 17, 0x27, KEY_0 }, // 29
        { "-", 246, 76, 17, 17, 0x2d, KEY_MINUS }, // 30
        { "=", 265, 76, 17, 17, 0x2e, KEY_EQUAL }, // 31
        { "Backspace", 291, 76, 30, 17, 0x2a, KEY_BACKSPACE }, // 32
        { "Play", 318, 76, 17, 17, 0x00 }, // 33
        { "Tab", 41, 95, 27, 17, 0x2b, KEY_TAB }, // 34
        { "Q", 65, 95, 17, 17, 0x14, KEY_Q }, // 35
        { "W", 84, 95, 17, 17, 0x1a, KEY_W }, // 36
        { "E", 103, 95, 17, 17, 0x08, KEY_E }, // 37
        { "R", 122, 95, 17, 17, 0x15, KEY_R }, // 38
        { "T", 141, 95, 17, 17, 0x17, KEY_T }, // 39
        { "Y", 160, 95, 17, 17, 0x1c, KEY_Y }, // 40
        { "U", 179, 95, 17, 17, 0x18, KEY_U }, // 41
        { "I", 198, 95, 17, 17, 0x0c, KEY_I }, // 42
        { "O", 217, 95, 17, 17, 0x12, KEY_O }, // 43
        { "P", 236, 95, 17, 17, 0x13, KEY_P }, // 44
        { "[", 255, 95, 17, 17, 0x2f, KEY_LEFTBRACE }, // 45
        { "]", 274, 95, 17, 17, 0x30, KEY_RIGHTBRACE }, // 46
        { "\\", 296, 95, 21, 17, 0x31, KEY_BACKSLASH }, // 47
        { "Stop", 318, 95, 17, 17, 0x00 }, // 48
        { "Caps", 43, 114, 31, 17, 0x39, KEY_CAPSLOCK }, // 49
        { "A", 70, 114, 17, 17, 0x04, KEY_A }, // 50
        { "S", 89, 114, 17, 17, 0x16, KEY_S }, // 51
        { "D", 108, 114, 17, 17, 0x07, KEY_D }, // 52
        { "F", 127, 114, 17, 17, 0x09, KEY_F }, // 53
        { "G", 146, 114, 17, 17, 0x0a, KEY_G }, // 54
        { "H", 165, 114, 17, 17, 0x0b, KEY_H }, // 55
        { "J", 184, 114, 17, 17, 0x0d, KEY_J }, // 56
        { "K", 203, 114, 17, 17, 0x0e, KEY_K }, // 57
        { "L", 222, 114, 17, 17, 0x0f, KEY_L }, // 58
        { ";", 241, 114, 17, 17, 0x33, KEY_SEMICOLON }, // 59
        { "'", 260, 114, 17, 17, 0x34, KEY_APOSTROPHE }, // 60
        { "Enter", 289, 114, 35, 17, 0x28, KEY_ENTER }, // 61
        { "Prev", 318, 114, 17, 17, 0x00 }, // 62
        { "Shift", 48, 133, 41, 17, 0xe1, KEY_LEFTSHIFT }, // 63
        { "Z", 79, 133, 17, 17, 0x1d, KEY_Z }, // 64
        { "X", 98, 133, 17, 17, 0x1b, KEY_X }, // 65
        { "C", 117, 133, 17, 17, 0x06, KEY_C }, // 66
        { "V", 136, 133, 17, 17, 0x19, KEY_V }, // 67
        { "B", 155, 133, 17, 17, 0x05, KEY_B }, // 68
        { "N", 174, 133, 17, 17, 0x11, KEY_N }, // 69
        { "M", 193, 133, 17, 17, 0x10, KEY_M }, // 70
        { ",", 212, 133, 17, 17, 0x36, KEY_COMMA }, // 71
        { ".", 231, 133, 17, 17, 0x37, KEY_DOT }, // 72
        { "/", 250, 133, 17, 17, 0x38, KEY_SLASH }, // 73
        { "Shift", 275, 133, 23, 17, 0xe5, KEY_RIGHTSHIFT }, // 74
        { "Up", 298, 133, 17, 17, 0x52, KEY_UP }, // 75
        { "Next", 318, 133, 17, 17, 0x00 }, // 76
        { "Ctrl", 38, 152, 21, 17, 0xe0, KEY_LEFTCTRL }, // 77
        { "Fn", 60, 152, 17, 17, 0x00 }, // 78
        { "Win", 79, 152, 17, 17, 0xe3, KEY_LEFTMETA }, // 79
        { "Alt", 98, 152, 17, 17, 0xe2, KEY_LEFTALT }, // 80
        { "Space", 155, 152, 94, 17, 0x2c, KEY_SPACE }, // 81
        { "Alt", 212, 152, 17, 17, 0xe6, KEY_RIGHTALT }, // 82
        { "PrtSc", 231, 152, 17, 17, 0x46, KEY_SYSRQ }, // 83
        // The firmware emits Meta + Shift + F23; the lamp binding is not Right Ctrl.
        { "Copilot", 250, 152, 17, 17, 0xe4, KEY_F23 }, // 84
        { "Left", 279, 152, 17, 17, 0x50, KEY_LEFT }, // 85
        { "Down", 298, 152, 17, 17, 0x51, KEY_DOWN }, // 86
        { "Right", 318, 152, 17, 17, 0x4f, KEY_RIGHT }, // 87
    } };
    return keys;
}
QVariantMap g615lwInputKeyCodes()
{
    QVariantMap result;
    const auto& keys = keyboardLayout();
    for (int id = 0; id < int(keys.size()); ++id)
        if (keys[id].inputCode)
            result.insert(QString::number(id), keys[id].inputCode);
    return result;
}
bool matchesKeyboardLamp(int id, quint32 x, quint32 y, uchar binding)
{
    if (id < 0 || id >= KeyboardLampCount)
        return false;
    const auto& key = keyboardLayout()[id];
    return x == quint32(key.x * 1000) && y == quint32(key.y * 1000) && binding == key.binding;
}
bool matchesStripLamp(int id, quint32 x, quint32 y)
{
    static constexpr std::array<std::pair<quint32, quint32>, 12> positions { { { 354000, 0 }, { 6000, 0 },
        { 360000, 0 }, { 0, 0 }, { 360000, 6000 }, { 0, 6000 }, { 360000, 259000 }, { 0, 259000 },
        { 360000, 265000 }, { 0, 265000 }, { 354000, 265000 }, { 6000, 265000 } } };
    return id >= 88 && id < 100 && positions[id - 88] == std::pair(x, y);
}
DeviceDescription g615lwDescription()
{
    DeviceDescription d;
    d.id = QStringLiteral("asus-g615lw");
    d.name = QStringLiteral("ASUS ROG Strix G16 G615LW");
    d.bodyColor = QColor(QStringLiteral("#333333"));
    d.keyColor = QColor(QStringLiteral("#111111"));
    d.capabilities = DeviceCapability::Brightness | DeviceCapability::Color | DeviceCapability::PerKeyColor
        | DeviceCapability::Lightbar | DeviceCapability::LightbarPower | DeviceCapability::PerStripColor;
    int id = 0;
    for (const auto& key : keyboardLayout())
        d.keys.append({ id++, QString::fromLatin1(key.label),
            QRectF(key.x - key.width / 2.0, key.y - key.height / 2.0, key.width, key.height) });
    DeviceStrip strip { FirstStripId, QCoreApplication::translate("Ambient::AsusG615lwLabels", "Lightbar"),
        { } };
    // Follow the reported chassis perimeter from the left rear to the right rear.
    // Hardware IDs alternate left/right and must not define gradient order.
    const auto& order = StripLampOrder;
    for (int i = 0; i < int(order.size()); ++i)
        strip.elements.append({ order[i], QString::number(i + 1), { } });
    // Manually described light distribution, in a compact top-view schematic.
    // Elements 2/5/8/11 are TL/BL/BR/TR. Each corner occupies about 10 mm;
    // the two other elements on each side blend across the rest of that side.
    // These display coordinates do not replace the verified hardware positions.
    constexpr qreal left = 16, right = 338, top = 25, bottom = 174;
    strip.path = { { order[0], { left + 5, top } }, { order[1], { left + 1, top } },
        { order[1], { left, top } }, { order[1], { left, top + 1 } }, { order[2], { left, top + 5 } },
        { order[3], { left, bottom - 5 } }, { order[4], { left, bottom - 1 } },
        { order[4], { left, bottom } }, { order[4], { left + 1, bottom } },
        { order[5], { left + 5, bottom } }, { order[6], { right - 5, bottom } },
        { order[7], { right - 1, bottom } }, { order[7], { right, bottom } },
        { order[7], { right, bottom - 1 } }, { order[8], { right, bottom - 5 } },
        { order[9], { right, top + 5 } }, { order[10], { right, top + 1 } }, { order[10], { right, top } },
        { order[10], { right - 1, top } }, { order[11], { right - 5, top } },
        { order[0], { left + 5, top } } };
    d.strips.append(strip);
    const auto sequence = [](int first, int last) {
        QList<int> ids;
        for (int i = first; i <= last; ++i)
            ids.append(i);
        return ids;
    };
    d.groups = { { "M1–M5", sequence(0, 4) }, { "WASD", { 36, 50, 51, 52 } },
        { QCoreApplication::translate("Ambient::AsusG615lwLabels", "Arrow keys"), { 75, 85, 86, 87 } },
        { QCoreApplication::translate("Ambient::AsusG615lwLabels", "Function keys"), sequence(6, 17) },
        { QCoreApplication::translate("Ambient::AsusG615lwLabels", "Number keys"), sequence(20, 29) },
        { QCoreApplication::translate("Ambient::AsusG615lwLabels", "Media keys"), { 33, 48, 62, 76 } } };
    return d;
}
}
