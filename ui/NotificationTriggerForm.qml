// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.FormLayout {
    id: form

    readonly property bool desktopNotification: true
    required property var options

    signal edited(string key, var value)

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
}
