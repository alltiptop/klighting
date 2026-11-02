// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

SettingsPage {
    id: page

    objectName: "AmbientEditor"
    title: qsTranslate("AmbientLed.AmbientPage", "Ambient")

    AmbientForm {
        controller: page.controller
        options: page.controller.effectPreset

        onEdited: (key, value) => page.controller.setAmbient(key, value)
    }
}
