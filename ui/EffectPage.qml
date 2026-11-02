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
    property string presetId: controller.effectPreset.id || ""
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
    objectName: "EffectEditor"
    title: controller.effectPreset.name || qsTranslate("AmbientLed.EffectPage", "Effect")

    actions: [
        Kirigami.Action {
            enabled: page.controller.selectedEffect >= 0 && page.controller.effectPresets.length < 64
            icon.name: "edit-copy"
            text: qsTranslate("AmbientLed.EffectPage", "Duplicate")

            onTriggered: {
                page.recorder.cancel();
                page.controller.addEffect(page.controller.effectPreset.type, true);
            }
        },
        Kirigami.Action {
            enabled: page.controller.selectedEffect >= 0
            icon.name: "document-export"
            text: qsTranslate("AmbientLed.EffectPage", "Export…")

            onTriggered: exportDialog.open()
        },
        Kirigami.Action {
            enabled: page.controller.selectedEffect >= 0
            icon.name: "edit-delete"
            text: qsTranslate("AmbientLed.EffectPage", "Remove")

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
                text: qsTranslate("AmbientLed.EffectPage",
                                  "Remove this effect preset? You can undo pending changes with Reset.")
                type: Kirigami.MessageType.Warning
                visible: page.removing

                actions: [
                    Kirigami.Action {
                        icon.name: "edit-delete"
                        text: qsTranslate("AmbientLed.EffectPage", "Remove")

                        onTriggered: {
                            page.recorder.cancel();
                            page.removing = false;
                            page.controller.removeEffect();
                        }
                    },
                    Kirigami.Action {
                        text: qsTranslate("AmbientLed.EffectPage", "Cancel")

                        onTriggered: page.removing = false
                    }
                ]
            }

            Kirigami.FormLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 36
                visible: page.controller.selectedEffect >= 0

                QQC2.TextField {
                    id: presetName

                    Kirigami.FormData.label: qsTranslate("AmbientLed.EffectPage", "Name:")
                    Layout.fillWidth: true
                    Layout.maximumWidth: Kirigami.Units.gridUnit * 20
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 20
                    maximumLength: 128
                    objectName: "EffectName"
                    text: page.controller.effectPreset.name || ""

                    onTextEdited: page.controller.setEffectName(text)
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
        nameFilters: [qsTranslate("AmbientLed.EffectPage", "Effect presets (*.json)")]
        title: qsTranslate("AmbientLed.EffectPage", "Export Effect Preset")

        onAccepted: page.controller.exportEffectFile(selectedFile)
    }

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        explanation: qsTranslate("AmbientLed.EffectPage",
                                 "Add a Solid Color, Ambient or Notifications preset, then assign it to keys or the lightbar.")
        text: qsTranslate("AmbientLed.EffectPage", "No effect presets")
        visible: page.controller.selectedEffect < 0
        width: parent.width - Kirigami.Units.largeSpacing * 2
    }

    Loader {
        active: page.controller.selectedEffect >= 0
        anchors.fill: parent
        sourceComponent: page.controller.effectPreset.type === "notifications" ? notificationsEditor :
                                                                                 page.controller.effectPreset.type
                                                                                 === "solid" ? solidEditor :
                                                                                               ambientEditor
    }

    Component {
        id: solidEditor

        SolidPage {
            controller: page.controller
            navigator: page.navigator
            recorder: page.recorder
        }
    }

    Component {
        id: ambientEditor

        AmbientPage {
            controller: page.controller
            navigator: page.navigator
            recorder: page.recorder
        }
    }

    Component {
        id: notificationsEditor

        NotificationsPage {
            controller: page.controller
            navigator: page.navigator
            recorder: page.recorder
        }
    }
}
