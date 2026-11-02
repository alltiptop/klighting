// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.FormLayout {
    id: form

    required property var controller
    readonly property bool desktopNotification: (options.trigger || "notification") === "notification"
    required property var options

    signal edited(string key, var value)

    QQC2.ComboBox {
        Kirigami.FormData.label: qsTranslate("AmbientLed.NotificationsForm", "Trigger:")
        Layout.maximumWidth: Kirigami.Units.gridUnit * 20
        Layout.preferredWidth: Kirigami.Units.gridUnit * 20
        currentIndex: form.desktopNotification ? 0 : 1
        model: [qsTranslate("AmbientLed.NotificationsForm", "Desktop notification"), qsTranslate(
                "AmbientLed.NotificationsForm", "Power connected")]
        objectName: "NotificationTrigger"

        onActivated: form.edited("trigger", ["notification", "power-connected"][currentIndex])
    }

    FormHint {
        text: qsTranslate("AmbientLed.NotificationsForm",
                          "Runs when external power is connected, not when the effect starts.")
        visible: !form.desktopNotification
    }

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

    QQC2.SpinBox {
        Kirigami.FormData.label: qsTranslate("AmbientLed.NotificationsForm", "Repeats:")
        editable: true
        from: 1
        objectName: "NotificationCycles"
        to: 10
        value: form.options.cycles || 3

        onValueModified: form.edited("cycles", value)
    }

    Item {
        Kirigami.FormData.isSection: true
        Kirigami.FormData.label: qsTranslate("AmbientLed.NotificationsForm", "Match notifications")
        visible: form.desktopNotification
    }

    QQC2.TextField {
        Kirigami.FormData.label: qsTranslate("AmbientLed.NotificationsForm", "Application:")
        Layout.fillWidth: true
        Layout.maximumWidth: Kirigami.Units.gridUnit * 20
        Layout.preferredWidth: Kirigami.Units.gridUnit * 20
        maximumLength: 256
        objectName: "NotificationApplication"
        placeholderText: qsTranslate("AmbientLed.NotificationsForm", "Any application")
        text: form.options.application || ""
        visible: form.desktopNotification

        onTextEdited: form.edited("application", text)
    }

    FormHint {
        text: qsTranslate("AmbientLed.NotificationsForm",
                          "Desktop ID (e.g. org.kde.kate) or notification app name, case-insensitive; not the window class.")
        visible: form.desktopNotification
    }

    QQC2.ComboBox {
        Kirigami.FormData.label: qsTranslate("AmbientLed.NotificationsForm", "Urgency:")
        Layout.maximumWidth: Kirigami.Units.gridUnit * 20
        Layout.preferredWidth: Kirigami.Units.gridUnit * 20
        currentIndex: (form.options.urgency ?? -1) + 1
        model: [qsTranslate("AmbientLed.NotificationsForm", "Any"), qsTranslate("AmbientLed.NotificationsForm",
                                                                                "Low"), qsTranslate(
                "AmbientLed.NotificationsForm", "Normal"), qsTranslate("AmbientLed.NotificationsForm",
                                                                       "Critical")]
        objectName: "NotificationUrgency"
        visible: form.desktopNotification

        onActivated: form.edited("urgency", currentIndex - 1)
    }

    QQC2.TextField {
        Kirigami.FormData.label: qsTranslate("AmbientLed.NotificationsForm", "Category:")
        Layout.fillWidth: true
        Layout.maximumWidth: Kirigami.Units.gridUnit * 20
        Layout.preferredWidth: Kirigami.Units.gridUnit * 20
        maximumLength: 256
        objectName: "NotificationCategory"
        placeholderText: qsTranslate("AmbientLed.NotificationsForm", "Any category (e.g. transfer.error)")
        text: form.options.category || ""
        visible: form.desktopNotification

        onTextEdited: form.edited("category", text)
    }

    QQC2.TextField {
        Kirigami.FormData.label: qsTranslate("AmbientLed.NotificationsForm", "KDE event ID:")
        Layout.fillWidth: true
        Layout.maximumWidth: Kirigami.Units.gridUnit * 20
        Layout.preferredWidth: Kirigami.Units.gridUnit * 20
        maximumLength: 256
        objectName: "NotificationEventId"
        placeholderText: qsTranslate("AmbientLed.NotificationsForm", "Any event")
        text: form.options.eventId || ""
        visible: form.desktopNotification

        onTextEdited: form.edited("eventId", text)
    }

    FormHint {
        text: qsTranslate("AmbientLed.NotificationsForm",
                          "All filled filters must match. Some apps do not supply a category or event ID.")
        visible: form.desktopNotification
    }

    EffectPowerForm {
        Layout.fillWidth: true
        options: form.options

        onEdited: (key, value) => {
            return form.edited(key, value);
        }
    }
}
