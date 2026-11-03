// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kcmutils as KCMUtils
import org.kde.kirigami as Kirigami

KCMUtils.AbstractKCM {
    id: page

    required property var controller
    required property var navigator
    property string presetId: controller.preset.id
    required property var recorder
    property bool removing: false

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
    framedView: false
    headerPaddingEnabled: false
    objectName: "PresetEditor"
    title: controller.preset.name

    actions: [
        Kirigami.Action {
            enabled: page.controller.presets.length < 64
            icon.name: "edit-copy"
            text: qsTranslate("AmbientLed.PresetPage", "Duplicate")

            onTriggered: {
                page.recorder.cancel();
                page.controller.addPreset(true);
            }
        },
        Kirigami.Action {
            icon.name: "document-export"
            text: qsTranslate("AmbientLed.PresetPage", "Export…")

            onTriggered: exportDialog.open()
        },
        Kirigami.Action {
            enabled: page.controller.presets.length > 1
            icon.name: "edit-delete"
            text: qsTranslate("AmbientLed.PresetPage", "Remove")

            onTriggered: page.removing = true
        }
    ]
    header: Kirigami.Padding {
        padding: Kirigami.Units.largeSpacing

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            Kirigami.InlineMessage {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 36
                text: (page.controller.preset.defaultDevices || []).length ? qsTranslate(
                                                                                 "AmbientLed.PresetPage",
                                                                                 "Remove this preset? Devices using it will have no default preset. You can undo pending changes with Reset.") :
                                                                             qsTranslate(
                                                                                 "AmbientLed.PresetPage",
                                                                                 "Remove this preset? You can undo pending changes with Reset.")
                type: Kirigami.MessageType.Warning
                visible: page.removing

                actions: [
                    Kirigami.Action {
                        icon.name: "edit-delete"
                        text: qsTranslate("AmbientLed.PresetPage", "Remove")

                        onTriggered: {
                            page.recorder.cancel();
                            page.removing = false;
                            page.controller.removePreset();
                        }
                    },
                    Kirigami.Action {
                        text: qsTranslate("AmbientLed.PresetPage", "Cancel")

                        onTriggered: page.removing = false
                    }
                ]
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

    FileDialog {
        id: exportDialog

        defaultSuffix: "json"
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTranslate("AmbientLed.PresetPage", "Lighting presets (*.json)")]
        title: qsTranslate("AmbientLed.PresetPage", "Export Lighting Preset")

        onAccepted: page.controller.exportFile(selectedFile)
    }

    KeyboardPage {
        anchors.fill: parent
        controller: page.controller
        navigator: page.navigator
        recorder: page.recorder
    }
}
