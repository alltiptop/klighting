// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import org.kde.kcmutils as KCMUtils
import org.kde.kirigami as Kirigami

KCMUtils.SimpleKCM {
    id: page

    required property var controller
    readonly property real formMaximumWidth: Kirigami.Units.gridUnit * 36
    required property var navigator
    required property var recorder
    property bool wideContent: false

    implicitHeight: Kirigami.Units.gridUnit * 34
    implicitWidth: Kirigami.Units.gridUnit * 46
    leftPadding: wideContent ? padding : Math.max(padding, (width - formMaximumWidth) / 2)
    padding: Kirigami.Units.largeSpacing
    rightPadding: leftPadding

    header: Kirigami.InlineMessage {
        text: page.controller.error
        type: Kirigami.MessageType.Error
        visible: text.length > 0
    }
}
