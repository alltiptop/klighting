// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Item {
    id: keyboardLayout

    readonly property color bodyColor: controller.device.bodyColor || Kirigami.Theme.backgroundColor
    readonly property real contentPadding: 8
    required property var controller
    readonly property bool editable: controller.editableKeys
    readonly property color keyColor: controller.device.keyColor || Kirigami.Theme.backgroundColor
    property int layerIndex: -1
    readonly property real layoutHeight: controller.device.height * scaleFactor + contentPadding * 2
    property bool livePreview: false
    readonly property color offColor: "#444444"
    readonly property var plainStrips: (controller.strips || []).filter(s => !s.path || !s.path.length)
    property var pressedKeys: []
    property var preview: controller.keyPreview(layerIndex)
    readonly property real scaleFactor: Math.max(1, width - contentPadding * 2) / Math.max(1,
                                                                                           controller.device.width)
    property var selection: []
    readonly property real stripHeight: Kirigami.Units.gridUnit * 2
    property bool wholeSelected: false

    signal wholeSelectionRequested(bool selected)

    function lightingColor(style) {
        if (style && style.mode === 2)
            return offColor;
        const color = style && (style.mode === 1 || style.opacity > 0) ? style.color :
                                                                         Kirigami.Theme.linkColor;
        const dimmed = Qt.tint(color, Qt.rgba(0, 0, 0, 1 - (style?.brightness ?? 1)));
        return Qt.colorEqual(dimmed, "#000000") ? offColor : dimmed;
    }

    function refreshPreview() {
        preview = livePreview ? controller.liveKeyPreview() : controller.keyPreview(layerIndex);
    }

    function toggle(id, additive) {
        if (!editable && !(controller.editableLightbar && (id === 65536 || (controller.strips || []).some(s
                                                                                                          => s.id
                                                                                                             === id || s.elements.some(
                                                                                                                 e => e.id
                                                                                                                      === id)))))
            return;

        let next = additive ? selection.slice() : [];
        const index = next.indexOf(id);
        if (index < 0)
            next.push(id);
        else
            next.splice(index, 1);
        selection = next;
    }

    Accessible.name: qsTranslate("AmbientLed.KeyboardView", "Keyboard layout")
    Accessible.role: Accessible.Grouping
    LayoutMirroring.childrenInherit: true
    // This represents physical hardware, even when the surrounding editor is RTL.
    LayoutMirroring.enabled: false
    implicitHeight: layoutHeight + plainStrips.length * stripHeight

    onEditableChanged: {
        if (!editable)
            selection = [];
    }
    onLayerIndexChanged: {
        selection = [];
        refreshPreview();
    }
    onLivePreviewChanged: refreshPreview()

    Connections {
        function onContentChanged() {
            keyboardLayout.refreshPreview();
        }

        function onPreviewChanged() {
            if (keyboardLayout.livePreview)
                keyboardLayout.refreshPreview();
        }

        target: keyboardLayout.controller
    }

    Rectangle {
        anchors.fill: parent
        color: keyboardLayout.bodyColor
        objectName: "KeyboardBody"
        radius: Kirigami.Units.cornerRadius
    }

    Repeater {
        id: keys

        model: keyboardLayout.controller.keys

        QQC2.AbstractButton {
            id: key

            required property int index
            readonly property color lightingColor: keyboardLayout.lightingColor(style)
            required property var modelData
            readonly property bool previewPressed: keyboardLayout.pressedKeys.indexOf(modelData.id) >= 0
            property bool selected: keyboardLayout.editable && (keyboardLayout.wholeSelected
                                                                || keyboardLayout.selection.indexOf(
                                                                    modelData.id) >= 0)
            property var style: keyboardLayout.preview[index] || {
                                    "mode": 0,
                                    "color": "#ffffff",
                                    "inherited": false
                                }

            Accessible.checkable: keyboardLayout.editable
            Accessible.checked: selected
            Accessible.description: style.inherited ? qsTranslate("AmbientLed.KeyboardView", "Inherits Base") :
                                                      style.mode === 0 ? qsTranslate("AmbientLed.KeyboardView",
                                                                                     "Ambient") : style.mode
                                                                         === 1 ? qsTranslate(
                                                                                     "AmbientLed.KeyboardView",
                                                                                     "Static color %1").arg(
                                                                                     style.color) :
                                                                                 qsTranslate(
                                                                                     "AmbientLed.KeyboardView",
                                                                                     "Off")
            Accessible.name: text
            QQC2.ToolTip.text: text + ": " + Accessible.description + (style.notificationId ? (
                                                                                                  style.notificationAbove
                                                                                                  ? qsTranslate(
                                                                                                        "AmbientLed.KeyboardView",
                                                                                                        "; Notifications above all layers") :
                                                                                                    qsTranslate(
                                                                                                        "AmbientLed.KeyboardView",
                                                                                                        "; Notifications")) :
                                                                                              "")
            QQC2.ToolTip.visible: hovered
            enabled: keyboardLayout.editable
            focusPolicy: Qt.StrongFocus
            height: modelData.height * keyboardLayout.scaleFactor
            text: modelData.label
            width: modelData.width * keyboardLayout.scaleFactor
            x: keyboardLayout.contentPadding + (modelData.x - keyboardLayout.controller.device.x)
               * keyboardLayout.scaleFactor

            y: keyboardLayout.contentPadding + (modelData.y - keyboardLayout.controller.device.y)
               * keyboardLayout.scaleFactor

            background: Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                border.color: key.lightingColor
                border.width: 1
                color: key.selected ? Qt.tint(keyboardLayout.keyColor, "#24ffffff") : keyboardLayout.keyColor
                objectName: "KeyColorBorder"
                radius: Kirigami.Units.cornerRadius

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -2
                    border.color: "white"
                    border.width: 1
                    color: "transparent"
                    objectName: "KeySelectionBorder"
                    radius: parent.radius + 2
                    visible: keyboardLayout.editable && (key.selected || key.visualFocus
                                                         || key.previewPressed)
                }
            }
            contentItem: QQC2.Label {
                color: key.lightingColor
                elide: Text.ElideRight
                font.bold: true
                font.family: Kirigami.Theme.smallFont.family
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                fontSizeMode: Text.Fit
                horizontalAlignment: Text.AlignHCenter
                minimumPointSize: 6
                objectName: "KeyLabel"
                text: key.text
                textFormat: Text.PlainText
                verticalAlignment: Text.AlignVCenter
            }

            onClicked: keyboardLayout.toggle(modelData.id, true)

            Kirigami.Icon {
                anchors.margins: 3
                anchors.right: parent.right
                anchors.top: parent.top
                color: key.lightingColor
                height: width
                isMask: true
                objectName: "KeyNotificationIcon"
                opacity: 0.65
                source: "notifications"
                visible: !!key.style.notificationId
                width: Math.min(12, parent.width / 4)
            }
        }
    }

    QQC2.AbstractButton {
        id: wholeKeyboard

        Accessible.name: qsTranslate("AmbientLed.KeyboardView", "Keyboard layout")
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        checkable: true
        checked: keyboardLayout.wholeSelected
        focusPolicy: Qt.StrongFocus
        height: keyboardLayout.layoutHeight
        objectName: "SelectWholeKeyboard"
        visible: !keyboardLayout.controller.device.perKey && keyboardLayout.controller.keys.length > 0

        background: Rectangle {
            border.color: "white"
            border.width: 1
            color: "transparent"
            objectName: "WholeKeyboardSelectionBorder"
            radius: Kirigami.Units.cornerRadius
            visible: keyboardLayout.wholeSelected || wholeKeyboard.visualFocus
        }

        onClicked: keyboardLayout.wholeSelectionRequested(!keyboardLayout.wholeSelected)
    }

    MouseArea {
        id: mouse

        property bool dragging: false
        property var previous: []
        property point start

        anchors.fill: parent
        enabled: keyboardLayout.editable
        preventStealing: true

        onCanceled: dragging = false
        onPositionChanged: event => {
            if (pressed && Math.abs(event.x - start.x) + Math.abs(event.y - start.y)
                    > Kirigami.Units.smallSpacing)
                dragging = true;
        }
        onPressed: event => {
            start = Qt.point(event.x, event.y);
            dragging = false;
            previous = event.modifiers & Qt.ControlModifier ? keyboardLayout.selection.slice() : [];
        }
        onReleased: event => {
            if (dragging) {
                let selected = previous.slice();
                const left = Math.min(start.x, event.x), top = Math.min(start.y, event.y);
                const right = Math.max(start.x, event.x), bottom = Math.max(start.y, event.y);
                for (let i = 0; i < keys.count; ++i) {
                    const key = keys.itemAt(i);
                    if (key.x < right && key.x + key.width > left && key.y < bottom && key.y + key.height
                            > top && selected.indexOf(keyboardLayout.controller.keys[i].id) < 0)
                        selected.push(keyboardLayout.controller.keys[i].id);
                }
                keyboardLayout.selection = selected;
            } else {
                let found = false;
                for (let i = 0; i < keys.count; ++i) {
                    const key = keys.itemAt(i);
                    if (event.x >= key.x && event.x <= key.x + key.width && event.y >= key.y && event.y
                            <= key.y + key.height) {
                        key.forceActiveFocus();
                        keyboardLayout.toggle(keyboardLayout.controller.keys[i].id, event.modifiers
                                              & Qt.ControlModifier);
                        found = true;
                        break;
                    }
                }
                if (!found)
                    keyboardLayout.selection = [];
            }
            dragging = false;
        }

        Rectangle {
            border.color: "white"
            color: "#24ffffff"
            height: Math.abs(mouse.mouseY - mouse.start.y)
            visible: mouse.pressed && mouse.dragging
            width: Math.abs(mouse.mouseX - mouse.start.x)
            x: Math.min(mouse.start.x, mouse.mouseX)
            y: Math.min(mouse.start.y, mouse.mouseY)
        }
    }

    Repeater {
        model: (keyboardLayout.controller.strips || []).filter(s => s.path && s.path.length)

        StripView {
            required property var modelData

            anchors.fill: parent
            controller: keyboardLayout.controller
            keyboard: keyboardLayout
            positioned: true
            strip: modelData
        }
    }

    Repeater {
        model: keyboardLayout.plainStrips

        StripView {
            required property int index
            required property var modelData

            controller: keyboardLayout.controller
            height: keyboardLayout.stripHeight
            keyboard: keyboardLayout
            strip: modelData
            width: parent.width
            y: keyboardLayout.layoutHeight + index * height
        }
    }
}
