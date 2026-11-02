// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Window
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

SettingsPage {
    id: page

    property var keyGroups: controller.groups()
    property int layerIndex: -1
    property string presetId: controller.preset.id
    property var previewWindow: visible && layerEditor.livePreviewEnabled && !recorder.recording
                                ? Window.window : null

    objectName: "KeyboardPage"
    title: layerIndex < 0 ? qsTranslate("AmbientLed.KeyboardPage", "Keyboard") : qsTranslate(
                                "AmbientLed.KeyboardPage", "Layer Keys")
    wideContent: true

    Component.onDestruction: {
        controller.watchLayerKeys(null);
        recorder.cancel();
    }
    onLayerIndexChanged: recorder.cancel()
    onPresetIdChanged: {
        recorder.cancel();
        layerIndex = -1;
        if (keyboardLayout)
            keyboardLayout.selection = [];
    }
    onPreviewWindowChanged: controller.watchLayerKeys(previewWindow)
    onVisibleChanged: {
        if (!visible)
            recorder.cancel();
    }

    Connections {
        function onContentChanged() {
            page.keyGroups = page.controller.groups();
            if (page.layerIndex >= page.controller.preset.layers.length)
                page.layerIndex = -1;
        }

        target: page.controller
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        LayerEditor {
            id: layerEditor

            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            Layout.maximumWidth: page.formMaximumWidth
            controller: page.controller
            enabled: page.controller.editableKeys || page.controller.editableLightbar
            layerIndex: page.layerIndex
            recorder: page.recorder

            onSelectionRequested: index => {
                page.recorder.cancel();
                page.layerIndex = index;
            }
        }

        QQC2.Label {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            Layout.maximumWidth: page.formMaximumWidth
            text: qsTranslate("AmbientLed.KeyboardPage",
                              "Only one layer is active: the most complex matching combination. Ties use the last row. Unassigned keys inherit Base.")
            visible: page.controller.preset.layers.length > 0
            wrapMode: Text.Wrap
        }

        Kirigami.InlineMessage {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            Layout.maximumWidth: page.formMaximumWidth
            text: !page.controller.device.perKey ? qsTranslate("AmbientLed.KeyboardPage",
                                                               "This device supports whole-keyboard lighting. Individual key selection is unavailable.") :
                                                   qsTranslate("AmbientLed.KeyboardPage",
                                                               "This preset belongs to another device. Its key assignments are preserved.")
            type: Kirigami.MessageType.Information
            visible: page.controller.keys.length > 0 && !page.controller.editableKeys
        }

        KeyboardView {
            id: keyboardLayout

            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            Layout.maximumWidth: 1200
            controller: page.controller
            layerIndex: page.layerIndex
            livePreview: page.visible && layerEditor.livePreviewEnabled && !page.recorder.recording
                         && page.controller.previewLayer >= 0
            objectName: "KeyboardView"
            pressedKeys: page.previewWindow ? page.controller.pressedKeys : []
        }

        Repeater {
            model: page.controller.strips

            StripEditor {
                required property var modelData

                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                Layout.maximumWidth: page.formMaximumWidth
                controller: page.controller
                keyboard: keyboardLayout
                layerIndex: page.layerIndex
                strip: modelData
            }
        }

        QQC2.Label {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            Layout.maximumWidth: page.formMaximumWidth
            text: qsTranslate("AmbientLed.KeyboardPage",
                              "Click or drag to select keys; Tab and Space also work.")
            visible: page.controller.editableKeys
            wrapMode: Text.Wrap
        }

        Kirigami.FormLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            Layout.maximumWidth: page.formMaximumWidth
            enabled: page.controller.editableKeys || page.controller.editableLightbar

            RowLayout {
                Kirigami.FormData.label: qsTranslate("AmbientLed.KeyboardPage", "Group:")

                QQC2.ComboBox {
                    id: groups

                    Accessible.name: qsTranslate("AmbientLed.KeyboardPage", "Key group")
                    Layout.fillWidth: true
                    model: page.keyGroups
                    objectName: "KeyGroupSelector"
                    textRole: "name"

                    onActivated: keyboardLayout.selection = page.keyGroups[currentIndex].ids
                }

                QQC2.Button {
                    text: qsTranslate("AmbientLed.KeyboardPage", "Select")

                    onClicked: keyboardLayout.selection = page.keyGroups[groups.currentIndex].ids
                }

                QQC2.ToolButton {
                    Accessible.name: qsTranslate("AmbientLed.KeyboardPage", "Remove group")
                    QQC2.ToolTip.text: Accessible.name
                    QQC2.ToolTip.visible: hovered
                    enabled: page.keyGroups[groups.currentIndex]?.custom ?? false
                    icon.name: "edit-delete"

                    onClicked: page.controller.removeGroup(page.keyGroups[groups.currentIndex].name)
                }
            }

            QQC2.Label {
                text: assignedEffects.assignmentState.defaults ? qsTranslate("AmbientLed.KeyboardPage",
                                                                             "Default layout") : qsTranslate(
                                                                     "AmbientLed.KeyboardPage",
                                                                     "%1 items selected").arg(
                                                                     keyboardLayout.selection.length)
            }
        }

        AssignedEffects {
            id: assignedEffects

            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            Layout.maximumWidth: page.formMaximumWidth
            controller: page.controller
            layerIndex: page.layerIndex
            selection: keyboardLayout.selection
        }

        Kirigami.FormLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            Layout.maximumWidth: page.formMaximumWidth

            RowLayout {
                Kirigami.FormData.label: qsTranslate("AmbientLed.KeyboardPage", "Save selection as:")

                QQC2.TextField {
                    id: groupName

                    Accessible.name: qsTranslate("AmbientLed.KeyboardPage", "Group name")
                    maximumLength: 64
                    placeholderText: qsTranslate("AmbientLed.KeyboardPage", "Group name")
                }

                QQC2.Button {
                    enabled: keyboardLayout.selection.length > 0 && groupName.text.trim().length > 0
                    text: qsTranslate("AmbientLed.KeyboardPage", "Save Group")

                    onClicked: page.controller.saveGroup(groupName.text, keyboardLayout.selection)
                }
            }
        }
    }
}
