// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import org.kde.kirigami as Kirigami

SettingsPage {
    id: page

    objectName: "SolidEditor"
    title: qsTranslate("AmbientLed.SolidPage", "Solid Color")

    Kirigami.FormLayout {
        HexColor {
            Kirigami.FormData.label: qsTranslate("AmbientLed.SolidPage", "Color:")
            label: qsTranslate("AmbientLed.SolidPage", "Solid color")
            text: page.controller.effectPreset.solidColor || "#ffffff"

            onEdited: color => page.controller.setSolidColor(color)
        }
    }
}
