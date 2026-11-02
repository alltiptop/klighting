// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Window
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: page

    required property var controller
    required property int layerIndex
    property alias livePreviewEnabled: livePreview.checked
    property int pendingLayer: -2
    required property var recorder
    readonly property real rowHeight: Math.max(Kirigami.Units.gridUnit * 2, baseChoice.implicitHeight,
                                               addLayerButton.implicitHeight) + Kirigami.Units.smallSpacing
                                      * 2

    signal selectionRequested(int index)

    function record(index) {
        const cancel = recorder.recording && pendingLayer === index;
        recorder.cancel();
        if (!cancel) {
            pendingLayer = index;
            recorder.start(page.Window.window);
        }
    }

    function remove(index) {
        recorder.cancel();
        const next = layerIndex >= index ? layerIndex - 1 : layerIndex;
        controller.removeLayer(index);
        selectionRequested(next);
    }

    spacing: Kirigami.Units.smallSpacing

    Connections {
        function onAccepted(conditions) {
            const index = page.pendingLayer;
            page.pendingLayer = -2;
            if (!page.visible || index < -1)
                return;
            if (index === -1) {
                const added = page.controller.addLayer(conditions);
                if (added >= 0)
                    page.selectionRequested(added);
            } else if (page.controller.setLayer(index, conditions)) {
                page.selectionRequested(index);
            }
        }

        function onChanged() {
            if (!page.recorder.recording)
                page.pendingLayer = -2;
        }

        target: page.recorder
    }

    Kirigami.Heading {
        level: 3
        text: qsTranslate("AmbientLed.LayerEditor", "Layers")
    }

    QQC2.ButtonGroup {
        id: choices
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0

        QQC2.RadioButton {
            id: baseChoice

            Layout.fillWidth: true
            Layout.preferredHeight: page.rowHeight
            QQC2.ButtonGroup.group: choices
            checked: page.layerIndex === -1
            objectName: "LayerChoiceBase"
            text: qsTranslate("AmbientLed.LayerEditor", "Base")

            onClicked: page.selectionRequested(-1)
        }

        Repeater {
            model: page.controller.preset.layers.length

            ColumnLayout {
                id: entry

                required property int index
                property var layerData: page.controller.preset.layers[index]

                Layout.fillWidth: true
                spacing: 0

                Kirigami.Separator {
                    Layout.fillWidth: true
                    objectName: "LayerSeparator" + entry.index
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.maximumHeight: page.rowHeight
                    Layout.minimumHeight: page.rowHeight
                    Layout.preferredHeight: page.rowHeight
                    objectName: "LayerRow" + entry.index
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.RadioButton {
                        id: layerChoice

                        Layout.fillHeight: true
                        Layout.fillWidth: true
                        QQC2.ButtonGroup.group: choices
                        QQC2.ToolTip.text: text
                        QQC2.ToolTip.visible: hovered && combinationLabel.truncated
                        checked: page.layerIndex === entry.index
                        objectName: "LayerChoice" + entry.index
                        text: entry.layerData?.summary || ""

                        contentItem: QQC2.Label {
                            id: combinationLabel

                            elide: Text.ElideRight
                            leftPadding: !layerChoice.mirrored ? layerChoice.indicator.width
                                                                 + layerChoice.spacing : 0
                            rightPadding: layerChoice.mirrored ? layerChoice.indicator.width
                                                                 + layerChoice.spacing : 0
                            text: layerChoice.text
                            verticalAlignment: Text.AlignVCenter
                        }

                        onClicked: page.selectionRequested(entry.index)
                    }

                    QQC2.Button {
                        icon.name: "list-add"
                        objectName: "RecordLayerKeys" + entry.index
                        text: page.recorder.recording && page.pendingLayer === entry.index ? qsTranslate(
                                                                                                 "AmbientLed.LayerEditor",
                                                                                                 "Cancel Recording") :
                                                                                             qsTranslate(
                                                                                                 "AmbientLed.LayerEditor",
                                                                                                 "Add")

                        onClicked: page.record(entry.index)
                    }

                    QQC2.ToolButton {
                        Accessible.name: qsTranslate("AmbientLed.LayerEditor", "Remove Layer")
                        QQC2.ToolTip.text: Accessible.name
                        QQC2.ToolTip.visible: hovered
                        icon.name: "edit-delete"
                        objectName: "RemoveLayer" + entry.index

                        onClicked: page.remove(entry.index)
                    }
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true

        QQC2.CheckBox {
            id: livePreview

            checked: false
            objectName: "LivePreview"
            text: qsTranslate("AmbientLed.LayerEditor", "Live Preview")
        }

        Item {
            Layout.fillWidth: true
        }

        QQC2.Button {
            id: addLayerButton

            enabled: page.controller.preset.layers.length < 32
            icon.name: "list-add"
            objectName: "AddLayer"
            text: page.recorder.recording && page.pendingLayer === -1 ? qsTranslate("AmbientLed.LayerEditor", "Cancel Recording") :
                                                                        qsTranslate("AmbientLed.LayerEditor",
                                                                                    "Add Layer")

            onClicked: page.record(-1)
        }
    }

    QQC2.Label {
        Layout.fillWidth: true
        text: (page.recorder.recording && page.recorder.captured.length > 0 ? page.controller.layerLabel(
                                                                                  page.recorder.captured)
                                                                              + "\n" : "")
              + page.recorder.hint
        visible: text.length > 0
        wrapMode: Text.Wrap
    }
}
