// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import Qt.labs.platform as Platform
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

RowLayout {
    id: root

    readonly property alias acceptableInput: field.acceptableInput
    property string buttonObjectName: "ColorSwatch"
    property string dialogObjectName: "ColorPicker"
    property string fieldObjectName: "HexColorInput"
    required property string label
    property string text: ""

    signal edited(string color)

    QQC2.TextField {
        id: field

        Accessible.name: root.label
        Layout.fillWidth: true
        Layout.maximumWidth: Kirigami.Units.gridUnit * 9
        LayoutMirroring.enabled: false
        horizontalAlignment: Text.AlignLeft
        maximumLength: 7
        objectName: root.fieldObjectName
        text: root.text

        validator: RegularExpressionValidator {
            regularExpression: /#[0-9a-fA-F]{6}/
        }

        onTextEdited: if (acceptableInput)
                          root.edited(text)
    }

    QQC2.Button {
        Accessible.description: qsTranslate("AmbientLed.HexColor", "Current color: %1").arg(field.text)
        Accessible.name: qsTranslate("AmbientLed.HexColor", "Choose %1").arg(root.label)
        implicitWidth: Kirigami.Units.gridUnit * 3
        objectName: root.buttonObjectName

        contentItem: Rectangle {
            border.color: Kirigami.Theme.disabledTextColor
            color: field.acceptableInput ? field.text : "transparent"
            implicitHeight: Kirigami.Units.gridUnit
            implicitWidth: Kirigami.Units.gridUnit * 2
        }

        onClicked: {
            picker.currentColor = field.acceptableInput ? field.text : "#ffffff";
            picker.open();
        }
    }

    // Use the same native dialog API as System Settings' Colors accent picker.
    Platform.ColorDialog {
        id: picker

        modality: Qt.WindowModal
        objectName: root.dialogObjectName
        title: root.label

        onAccepted: root.edited("#" + color.toString().slice(-6))
    }
}
