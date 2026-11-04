// SPDX-License-Identifier: GPL-3.0-or-later
#include "fa706_layout.h"
#include <initializer_list>
#include <linux/input-event-codes.h>

namespace Ambient
{
DeviceDescription fa706Description()
{
    DeviceDescription description;
    description.id = QStringLiteral("asus-tuf-fa706ihrb");
    description.name = QStringLiteral("ASUS TUF Gaming A17 FA706IHRB");
    description.capabilities = DeviceCapability::Brightness | DeviceCapability::Color;
    description.bodyColor = QColor("#343434");
    description.keyColor = QColor("#171717");
    // ANSI schematic only: evdev IDs identify preview keys, never physical RGB lamps.
    const auto key = [&](int code, const char* label, qreal x, qreal y, qreal width = 1, qreal height = 1) {
        description.keys.append(
            { code, QString::fromLatin1(label), { x * 20, y * 20, width * 20 - 2, height * 20 - 2 } });
    };
    struct Key {
        int code;
        const char* label;
        qreal width = 1;
    };
    const auto row = [&](qreal x, qreal y, std::initializer_list<Key> keys) {
        for (const auto& item : keys) {
            key(item.code, item.label, x, y, item.width);
            x += item.width;
        }
    };
    key(KEY_ESC, "Esc", 0, 0, 1, 0.7);
    const int functionCodes[] = { KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9,
        KEY_F10, KEY_F11, KEY_F12 };
    for (int i = 0; i < 12; ++i) {
        const auto label = QStringLiteral("F%1").arg(i + 1).toLatin1();
        key(functionCodes[i], label.constData(), 2 + i + (i / 4) * 0.5, 0, 1, 0.7);
    }
    key(KEY_DELETE, "Del", 15.5, 0, 1, 0.7);
    key(KEY_PAUSE, "Pause", 16.5, 0, 1, 0.7);
    key(KEY_SYSRQ, "PrtSc", 17.5, 0, 1, 0.7);
    key(KEY_HOME, "Home", 18.5, 0, 1, 0.7);
    row(0, 1.1,
        { { KEY_GRAVE, "`" }, { KEY_1, "1" }, { KEY_2, "2" }, { KEY_3, "3" }, { KEY_4, "4" }, { KEY_5, "5" },
            { KEY_6, "6" }, { KEY_7, "7" }, { KEY_8, "8" }, { KEY_9, "9" }, { KEY_0, "0" },
            { KEY_MINUS, "-" }, { KEY_EQUAL, "=" }, { KEY_BACKSPACE, "Backspace", 2 } });
    row(0, 2.1,
        { { KEY_TAB, "Tab", 1.5 }, { KEY_Q, "Q" }, { KEY_W, "W" }, { KEY_E, "E" }, { KEY_R, "R" },
            { KEY_T, "T" }, { KEY_Y, "Y" }, { KEY_U, "U" }, { KEY_I, "I" }, { KEY_O, "O" }, { KEY_P, "P" },
            { KEY_LEFTBRACE, "[" }, { KEY_RIGHTBRACE, "]" }, { KEY_BACKSLASH, "\\", 1.5 } });
    row(0, 3.1,
        { { KEY_CAPSLOCK, "Caps", 1.75 }, { KEY_A, "A" }, { KEY_S, "S" }, { KEY_D, "D" }, { KEY_F, "F" },
            { KEY_G, "G" }, { KEY_H, "H" }, { KEY_J, "J" }, { KEY_K, "K" }, { KEY_L, "L" },
            { KEY_SEMICOLON, ";" }, { KEY_APOSTROPHE, "'" }, { KEY_ENTER, "Enter", 2.25 } });
    row(0, 4.1,
        { { KEY_LEFTSHIFT, "Shift", 2.25 }, { KEY_Z, "Z" }, { KEY_X, "X" }, { KEY_C, "C" }, { KEY_V, "V" },
            { KEY_B, "B" }, { KEY_N, "N" }, { KEY_M, "M" }, { KEY_COMMA, "," }, { KEY_DOT, "." },
            { KEY_SLASH, "/" }, { KEY_RIGHTSHIFT, "Shift", 2.75 } });
    row(0, 5.1,
        { { KEY_LEFTCTRL, "Ctrl", 1.25 }, { KEY_FN, "Fn" }, { KEY_LEFTMETA, "Super" },
            { KEY_LEFTALT, "Alt", 1.25 }, { KEY_SPACE, "Space", 5.5 }, { KEY_RIGHTALT, "Alt", 1.25 },
            { KEY_RIGHTCTRL, "Ctrl", 1.25 } });
    key(KEY_UP, "Up", 13.5, 5.1, 1, 0.55);
    key(KEY_LEFT, "Left", 12.5, 5.65, 1, 0.55);
    key(KEY_DOWN, "Down", 13.5, 5.65, 1, 0.55);
    key(KEY_RIGHT, "Right", 14.5, 5.65, 1, 0.55);
    row(15.5, 1.1,
        { { KEY_NUMLOCK, "Num" }, { KEY_KPSLASH, "/" }, { KEY_KPASTERISK, "*" }, { KEY_KPMINUS, "-" } });
    row(15.5, 2.1, { { KEY_KP7, "7" }, { KEY_KP8, "8" }, { KEY_KP9, "9" } });
    row(15.5, 3.1, { { KEY_KP4, "4" }, { KEY_KP5, "5" }, { KEY_KP6, "6" } });
    row(15.5, 4.1, { { KEY_KP1, "1" }, { KEY_KP2, "2" }, { KEY_KP3, "3" } });
    row(15.5, 5.1, { { KEY_KP0, "0", 2 }, { KEY_KPDOT, "." } });
    key(KEY_KPPLUS, "+", 18.5, 2.1, 1, 2);
    key(KEY_KPENTER, "Enter", 18.5, 4.1, 1, 2);
    return description;
}

QVariantMap fa706InputKeyCodes()
{
    QVariantMap result;
    for (const auto& key : fa706Description().keys)
        if (key.id != KEY_FN) // Fn is handled by firmware, not a normal evdev key.
            result.insert(QString::number(key.id), key.id);
    return result;
}
}
