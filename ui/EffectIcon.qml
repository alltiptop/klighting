// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import org.kde.kirigami as Kirigami

Item {
    id: root

    property color color: "#ffffff"
    property string type: "ambient"

    implicitHeight: implicitWidth
    implicitWidth: Kirigami.Units.iconSizes.smallMedium

    Rectangle {
        anchors.centerIn: parent
        border.color: Kirigami.Theme.disabledTextColor
        border.width: 1
        color: root.color
        height: width
        objectName: "SolidColorCircle"
        radius: width / 2
        visible: root.type === "solid"
        width: parent.width * 0.75
    }

    Kirigami.Icon {
        anchors.fill: parent
        source: root.type === "notifications" ? "notifications" : "video-display"
        visible: root.type !== "solid"
    }
}
