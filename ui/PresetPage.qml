// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kcmutils as KCMUtils
import org.kde.kirigami as Kirigami

KCMUtils.SimpleKCM {
    id: page

    readonly property bool addingLayer: recorder.recording && controller.selectedLayer < 0
    required property var controller
    required property var navigator
    property int pendingLayer: -1
    property string presetId: controller.preset.id
    required property var recorder
    property int removalLayer: -1
    property bool removing: false

    function recordLayer() {
        const index = controller.selectedLayer;
        const cancel = recorder.recording && pendingLayer === index;
        recorder.cancel();
        if (!cancel) {
            pendingLayer = index;
            recorder.start(page.Window.window);
        }
    }

    Kirigami.ColumnView.reservedSpace: {
        const view = Kirigami.ColumnView.view;
        if (!view)
            return 0;
        if (navigator.nativeSection)
            return view.width >= Kirigami.Units.gridUnit * 42 ? view.columnWidth : 0;
        return view.columnWidth * (view.width >= Kirigami.Units.gridUnit * 60 ? 2 : view.width
                                                                                >= Kirigami.Units.gridUnit
                                                                                * 36 ? 1 : 0);
    }
    objectName: "PresetEditor"
    padding: Kirigami.Units.largeSpacing
    title: controller.selectedLayer < 0 ? controller.preset.name : controller.preset.layers[controller.selectedLayer]
                                          ?.summary || ""

    actions: [
        Kirigami.Action {
            enabled: page.controller.presets.length < 64
            icon.name: "edit-copy"
            text: qsTranslate("AmbientLed.PresetPage", "Duplicate")
            visible: page.controller.selectedLayer < 0 && !page.addingLayer

            onTriggered: {
                page.recorder.cancel();
                page.controller.addPreset(true);
            }
        },
        Kirigami.Action {
            icon.name: "document-export"
            text: qsTranslate("AmbientLed.PresetPage", "Export…")
            visible: page.controller.selectedLayer < 0 && !page.addingLayer

            onTriggered: exportDialog.open()
        },
        Kirigami.Action {
            icon.name: "configure"
            objectName: "RecordLayerKeys" + page.controller.selectedLayer
            text: page.recorder.recording && page.pendingLayer >= 0 ? qsTranslate("AmbientLed.LayerEditor",
                                                                                  "Cancel Recording") :
                                                                      qsTranslate("AmbientLed.LayerEditor",
                                                                                  "Change combination…")
            visible: page.controller.selectedLayer >= 0

            onTriggered: page.recordLayer()
        },
        Kirigami.Action {
            enabled: page.controller.selectedLayer >= 0 || page.controller.preset.id
                     !== page.controller.defaultId
            icon.name: "edit-delete"
            objectName: page.controller.selectedLayer >= 0 ? "RemoveLayer" + page.controller.selectedLayer :
                                                             "RemovePreset"
            text: qsTranslate("AmbientLed.PresetPage", "Remove")
            visible: !page.addingLayer

            onTriggered: {
                page.recorder.cancel();
                page.removalLayer = page.controller.selectedLayer;
                page.removing = true;
            }
        }
    ]
    header: Kirigami.Padding {
        padding: Kirigami.Units.largeSpacing
        visible: !page.addingLayer

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            Kirigami.InlineMessage {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 36
                text: page.controller.error
                type: Kirigami.MessageType.Error
                visible: text.length > 0
            }

            Kirigami.FormLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 36

                QQC2.TextField {
                    id: presetName

                    Kirigami.FormData.label: qsTranslate("AmbientLed.PresetPage", "Name:")
                    Layout.fillWidth: true
                    Layout.maximumWidth: Kirigami.Units.gridUnit * 20
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 20
                    enabled: page.controller.selectedLayer < 0
                    maximumLength: 128
                    objectName: "PresetName"
                    text: page.controller.preset.name

                    onTextEdited: page.controller.setName(text)
                }
            }
        }
    }

    onPresetIdChanged: {
        removing = false;
        recorder.cancel();
    }
    onVisibleChanged: {
        if (!visible)
            recorder.cancel();
    }

    Connections {
        function onAccepted(conditions) {
            const index = page.pendingLayer;
            page.pendingLayer = -1;
            if (page.visible && index >= 0)
                page.controller.setLayer(index, conditions);
        }

        function onChanged() {
            if (!page.recorder.recording)
                page.pendingLayer = -1;
        }

        target: page.recorder
    }

    Loader {
        active: page.recorder.recording && page.pendingLayer >= 0

        sourceComponent: ModalDialog {
            objectName: "RecordLayerDialog"
            standardButtons: QQC2.Dialog.Cancel
            text: (page.recorder.captured.length ? page.controller.layerLabel(page.recorder.captured)
                                                   + "\n\n" : "") + page.recorder.hint
            title: qsTranslate("AmbientLed.PresetPage", "Recording key combination")

            onClosed: page.recorder.cancel()
            onRejected: page.recorder.cancel()
        }
    }

    Loader {
        active: page.removing

        sourceComponent: ModalDialog {
            objectName: "RemovePresetDialog"
            text: page.removalLayer >= 0 ? qsTranslate("AmbientLed.PresetPage",
                                                       "Remove this layer? Its settings will be removed for all devices in this preset.") :
                                           qsTranslate("AmbientLed.PresetPage",
                                                       "Remove this preset? You can undo pending changes with Reset.")
            title: page.removalLayer >= 0 ? qsTranslate("AmbientLed.LayerEditor", "Remove Layer") :
                                            qsTranslate("AmbientLed.PresetPage", "Remove")

            onAccepted: {
                if (page.removalLayer >= 0) {
                    page.controller.removeLayer(page.removalLayer);
                    page.controller.selectedLayer = -1;
                } else {
                    page.controller.removePreset();
                }
            }
            // Keep accepted connected when closed is emitted first.
            onClosed: Qt.callLater(() => page.removing = false)
        }
    }

    FileDialog {
        id: exportDialog

        defaultSuffix: "json"
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTranslate("AmbientLed.PresetPage", "Lighting presets (*.json)")]
        title: qsTranslate("AmbientLed.PresetPage", "Export Lighting Preset")

        onAccepted: page.controller.exportFile(selectedFile)
    }

    Item {
        implicitHeight: page.addingLayer ? page.availableHeight : keyboard.implicitHeight

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            explanation: page.recorder.hint
            objectName: "NewLayerPrompt"
            text: page.recorder.captured.length ? page.controller.layerLabel(page.recorder.captured) : ""
            visible: page.addingLayer
            width: parent.width - Kirigami.Units.largeSpacing * 2
        }

        KeyboardPage {
            id: keyboard

            documentController: page.controller
            navigator: page.navigator
            recorder: page.recorder
            visible: !page.addingLayer
            width: parent.width
        }
    }
}
