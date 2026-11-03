// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

QQC2.Dialog {
    id: dialog

    required property string text

    anchors.centerIn: parent
    modal: true
    parent: QQC2.Overlay.overlay
    standardButtons: QQC2.Dialog.Ok | QQC2.Dialog.Cancel
    width: Math.min(Kirigami.Units.gridUnit * 24, parent.width - Kirigami.Units.largeSpacing * 2)

    contentItem: QQC2.Label {
        text: dialog.text
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
    }

    Component.onCompleted: open()
}
