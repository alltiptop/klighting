// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

SettingsPage {
    id: page

    objectName: "NotificationsEditor"
    title: qsTranslate("AmbientLed.NotificationsPage", "Notifications")

    NotificationsForm {
        controller: page.controller
        options: page.controller.effectPreset

        onEdited: (key, value) => page.controller.setNotification(key, value)
    }
}
