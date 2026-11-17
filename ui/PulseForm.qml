// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.FormLayout {
    id: form

    required property var options

    signal edited(string key, var value)

    HexColor {
        Kirigami.FormData.label: qsTranslate("AmbientLed.NotificationsForm", "Color:")
        buttonObjectName: "ChooseNotificationColor"
        dialogObjectName: "NotificationColorDialog"
        fieldObjectName: "NotificationColor"
        label: qsTranslate("AmbientLed.NotificationsForm", "Notification color")
        text: form.options.notificationColor || "#3daee9"

        onEdited: color => {
            return form.edited("color", color);
        }
    }

    QQC2.ComboBox {
        Kirigami.FormData.label: qsTranslate("AmbientLed.NotificationsForm", "Pattern:")
        Layout.maximumWidth: Kirigami.Units.gridUnit * 20
        Layout.preferredWidth: Kirigami.Units.gridUnit * 20
        currentIndex: ["pulse", "blink", "double"].indexOf(form.options.pattern)
        model: [qsTranslate("AmbientLed.NotificationsForm", "Smooth pulse"), qsTranslate(
                "AmbientLed.NotificationsForm", "Blink"), qsTranslate("AmbientLed.NotificationsForm",
                                                                      "Double blink")]
        objectName: "NotificationPattern"

        onActivated: form.edited("pattern", ["pulse", "blink", "double"][currentIndex])
    }

    RowLayout {
        Kirigami.FormData.label: qsTranslate("AmbientLed.NotificationsForm", "Cycle duration:")

        QQC2.SpinBox {
            Accessible.name: qsTranslate("AmbientLed.NotificationsForm", "Cycle duration in milliseconds")
            editable: true
            from: 500
            objectName: "NotificationPeriod"
            stepSize: 100
            to: 5000
            value: form.options.period || 1000

            onValueModified: form.edited("period", value)
        }

        QQC2.Label {
            text: qsTranslate("AmbientLed.NotificationsForm", "ms")
        }
    }
}
