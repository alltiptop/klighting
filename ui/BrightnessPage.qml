// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

SettingsPage {
    id: page

    objectName: "BrightnessEditor"
    title: qsTranslate("AmbientLed.BrightnessForm", "Brightness")

    BrightnessForm {
        options: page.controller.effectPreset

        onEdited: value => page.controller.setBrightness(value)
    }
}
