// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

QQC2.Label {
    Layout.fillWidth: true
    Layout.maximumWidth: Math.min(Kirigami.Units.gridUnit * 24, parent.width)
    Layout.preferredWidth: Kirigami.Units.gridUnit * 24
    textFormat: Text.PlainText
    wrapMode: Text.Wrap
}
