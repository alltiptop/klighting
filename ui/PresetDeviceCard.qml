// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

QQC2.Frame {
    id: card

    property bool collapsible: true
    property bool confirmingPaste: false
    property bool confirmingRemoval: false
    required property var controller
    required property var documentController
    property bool expanded: false
    readonly property var info: controller.profileInfo
    property int layerIndex: -1
    property bool livePreview: false
    property string profileId: ""
    property var selection: []
    property var warnings: []
    property bool wholeDevice: false

    signal activated

    function sameSelection(ids) {
        return ids.length === selection.length && ids.every((id, index) => id === selection[index]);
    }

    objectName: "PresetDeviceCard"
    padding: Kirigami.Units.largeSpacing

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.ItemDelegate {
                id: deviceName

                Accessible.name: text
                Layout.alignment: Qt.AlignVCenter
                Layout.fillWidth: true
                bottomPadding: topPadding
                font.bold: true
                objectName: "SelectPresetDevice"
                text: card.info.name
                topPadding: Kirigami.Units.smallSpacing

                contentItem: QQC2.Label {
                    Accessible.ignored: true
                    color: !card.info.enabled ? Kirigami.Theme.disabledTextColor : deviceName.down
                                                ? Kirigami.Theme.highlightedTextColor :
                                                  Kirigami.Theme.textColor
                    elide: Text.ElideRight
                    font: deviceName.font
                    text: deviceName.text
                    verticalAlignment: Text.AlignVCenter
                }

                onClicked: {
                    card.activated();
                    if (card.collapsible)
                        card.expanded = !card.expanded;
                }
            }

            QQC2.ToolButton {
                Accessible.name: card.expanded ? qsTranslate("AmbientLed.PresetDeviceCard",
                                                             "Collapse device") : qsTranslate(
                                                     "AmbientLed.PresetDeviceCard", "Expand device")
                Layout.alignment: Qt.AlignVCenter
                QQC2.ToolTip.text: Accessible.name
                QQC2.ToolTip.visible: hovered
                icon.name: card.expanded ? "arrow-down" : "arrow-right"
                objectName: "ToggleDeviceCard"
                visible: card.collapsible

                onClicked: card.expanded = !card.expanded
            }

            QQC2.ToolButton {
                Accessible.name: qsTranslate("AmbientLed.PresetDeviceCard", "Copy device profile")
                Layout.alignment: Qt.AlignVCenter
                QQC2.ToolTip.text: Accessible.name
                QQC2.ToolTip.visible: hovered
                icon.name: "edit-copy"
                objectName: "CopyDeviceProfile"

                onClicked: card.controller.copyProfile()
            }

            QQC2.ToolButton {
                Accessible.name: qsTranslate("AmbientLed.PresetDeviceCard", "Paste device profile")
                Layout.alignment: Qt.AlignVCenter
                QQC2.ToolTip.text: Accessible.name
                QQC2.ToolTip.visible: hovered
                enabled: card.controller.canPasteProfile
                icon.name: "edit-paste"
                objectName: "PasteDeviceProfile"

                onClicked: {
                    card.expanded = true;
                    card.warnings = card.controller.pasteWarnings();
                    if (card.warnings.length > 0)
                        card.confirmingPaste = true;
                    else
                        card.controller.pasteProfile();
                }
            }

            QQC2.ToolButton {
                Accessible.name: qsTranslate("AmbientLed.PresetDeviceCard", "Remove device from preset")
                Layout.alignment: Qt.AlignVCenter
                QQC2.ToolTip.text: Accessible.name
                QQC2.ToolTip.visible: hovered
                icon.name: "edit-delete"
                objectName: "RemovePresetDevice"

                onClicked: card.confirmingRemoval = true
            }
        }

        Kirigami.InlineMessage {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            Layout.maximumWidth: Kirigami.Units.gridUnit * 36
            objectName: "DisabledDeviceMessage"
            text: qsTranslate("AmbientLed.PresetDeviceCard",
                              "This device is disabled. You can still edit its profile.")
            type: Kirigami.MessageType.Warning
            visible: card.expanded && !card.info.enabled
        }

        Kirigami.InlineMessage {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            Layout.maximumWidth: Kirigami.Units.gridUnit * 36
            text: qsTranslate("AmbientLed.PresetDeviceCard",
                              "Connect this device to edit its layout. Its saved settings are preserved.")
            type: Kirigami.MessageType.Information
            visible: card.expanded && !card.info.layoutAvailable
        }

        Kirigami.InlineMessage {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            Layout.maximumWidth: Kirigami.Units.gridUnit * 36
            text: card.warnings.join("\n")
            type: Kirigami.MessageType.Warning
            visible: card.expanded && card.confirmingPaste

            actions: [
                Kirigami.Action {
                    enabled: card.controller.canPasteProfile
                    icon.name: "edit-paste"
                    text: qsTranslate("AmbientLed.PresetDeviceCard", "Paste compatible settings")

                    onTriggered: {
                        card.controller.pasteProfile();
                        card.confirmingPaste = false;
                    }
                },
                Kirigami.Action {
                    text: qsTranslate("AmbientLed.PresetDeviceCard", "Cancel")

                    onTriggered: card.confirmingPaste = false
                }
            ]
        }

        Kirigami.InlineMessage {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            Layout.maximumWidth: Kirigami.Units.gridUnit * 36
            text: !card.controller.device.perKey ? qsTranslate("AmbientLed.KeyboardPage",
                                                               "This device supports whole-keyboard lighting. Individual key selection is unavailable.") :
                                                   qsTranslate("AmbientLed.KeyboardPage",
                                                               "This preset belongs to another device. Its key assignments are preserved.")
            type: Kirigami.MessageType.Information
            visible: card.expanded && card.controller.keys.length > 0 && !card.controller.editableKeys
        }

        Loader {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            Layout.maximumWidth: 1200
            active: card.expanded && card.info.layoutAvailable
            asynchronous: true
            visible: active

            sourceComponent: KeyboardView {
                id: mapping

                property bool initialized: false

                controller: card.controller
                layerIndex: card.layerIndex
                livePreview: card.livePreview && card.controller.previewLayer >= 0
                objectName: "KeyboardView"
                pressedKeys: card.livePreview ? card.controller.pressedKeys : []
                wholeSelected: card.wholeDevice

                Component.onCompleted: {
                    selection = card.selection;
                    initialized = true;
                }
                onSelectionChanged: {
                    if (initialized && !card.sameSelection(selection)) {
                        card.selection = selection;
                        card.activated();
                    }
                }
                onWholeSelectionRequested: selected => {
                    card.selection = [];
                    card.wholeDevice = selected;
                    card.activated();
                }

                Connections {
                    function onSelectionChanged() {
                        if (!card.sameSelection(mapping.selection))
                            mapping.selection = card.selection;
                    }

                    target: card
                }
            }
        }

        QQC2.CheckBox {
            Accessible.description: card.info.name
            checkState: card.wholeDevice ? Qt.Checked : card.selection.length ? Qt.PartiallyChecked :
                                                                                Qt.Unchecked

            nextCheckState: () => card.wholeDevice || card.selection.length ? Qt.Unchecked : Qt.Checked
            objectName: "SelectWholeDevice"
            text: qsTranslate("AmbientLed.PresetDeviceCard", "Select all")
            visible: card.expanded

            onClicked: {
                const selectWholeDevice = checkState === Qt.Checked;
                card.selection = [];
                card.wholeDevice = selectWholeDevice;
                card.activated();
            }
        }
    }

    Component.onCompleted: {
        profileId = info.id;
        expanded = !collapsible || info.enabled;
    }
    onCollapsibleChanged: {
        if (!collapsible)
            expanded = true;
    }
    onLayerIndexChanged: selection = []
    onSelectionChanged: {
        if (selection.length)
            wholeDevice = false;
    }

    Connections {
        function onContentChanged() {
            if (card.confirmingPaste)
                card.warnings = card.controller.pasteWarnings();
        }

        target: card.controller
    }

    Loader {
        active: card.confirmingRemoval

        sourceComponent: ModalDialog {
            objectName: "RemoveDeviceDialog"
            text: qsTranslate("AmbientLed.PresetDeviceCard",
                              "Remove %1 from this preset? This removes its settings from Base and all layers.").arg(
                      card.info.name)
            title: qsTranslate("AmbientLed.PresetDeviceCard", "Remove device from preset")

            onAccepted: card.documentController.removePresetDevice(card.info.id)
            // closed can precede accepted; keep the handler alive until confirmation is processed.
            onClosed: Qt.callLater(() => card.confirmingRemoval = false)
        }
    }
}
