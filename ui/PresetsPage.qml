// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

SidebarPage {
    id: page

    readonly property var filteredEntries: {
        const query = search.text.trim().toLocaleLowerCase();
        let rows = [];
        controller.presets.forEach((preset, sourceIndex) => {
            const parentMatches = preset.name.toLocaleLowerCase().includes(query);
            const layers = preset.layers.map((name, layerIndex) => ({
                name,
                layerIndex
            })).filter(layer => parentMatches || layer.name.toLocaleLowerCase().includes(query));
            if (!parentMatches && !layers.length)
                return;
            rows.push({
                          id: preset.id,
                          name: preset.name,
                          sourceIndex,
                          layerIndex: -1,
                          layerCount: preset.layers.length
                      });
            layers.forEach(layer => rows.push({
                                                  id: preset.id,
                                                  name: layer.name,
                                                  sourceIndex,
                                                  layerIndex: layer.layerIndex,
                                                  layerCount: preset.layers.length
                                              }));
        });
        return rows;
    }
    property string recordingPreset: ""

    function recordLayer(presetIndex) {
        const cancel = recorder.recording && recordingPreset === controller.presets[presetIndex].id;
        selectEntry(presetIndex, -1);
        if (!cancel) {
            recordingPreset = controller.preset.id;
            recorder.start(page.Window.window);
        }
    }

    function selectEntry(presetIndex, layerIndex) {
        recorder.cancel();
        controller.selected = presetIndex;
        controller.selectedLayer = layerIndex;
        showEditor();
    }

    function showEditor() {
        if (navigator.depth < navigationLevel + 2)
            open("PresetPage.qml");
        else
            navigator.currentIndex = navigationLevel + 1;
    }

    extraFooterTopPadding: true
    navigationLevel: 1
    objectName: "PresetSidebar"
    title: qsTranslate("AmbientLed.PresetsPage", "Presets")

    footer: RowLayout {
        QQC2.Button {
            enabled: page.controller.presets.length < 64
            icon.name: "list-add"
            objectName: "AddPreset"
            text: qsTranslate("AmbientLed.PresetsPage", "New")

            onClicked: {
                search.clear();
                page.recorder.cancel();
                if (page.controller.addPreset(false))
                    page.showEditor();
            }
        }

        QQC2.ToolButton {
            Accessible.name: qsTranslate("AmbientLed.PresetsPage", "Import Preset…")
            QQC2.ToolTip.text: Accessible.name
            QQC2.ToolTip.visible: hovered
            enabled: page.controller.presets.length < 64
            icon.name: "document-import"

            onClicked: importDialog.open()
        }
    }
    header: ColumnLayout {
        Kirigami.SearchField {
            id: search

            Accessible.name: qsTranslate("AmbientLed.PresetsPage", "Filter presets")
            Layout.fillWidth: true
            objectName: "PresetSearch"
            placeholderText: qsTranslate("AmbientLed.PresetsPage", "Search…")
        }
    }
    view: ListView {
        id: presets

        currentIndex: page.filteredEntries.findIndex(entry => {
            return entry.sourceIndex === page.controller.selected && entry.layerIndex
                    === page.controller.selectedLayer;
        })
        model: page.filteredEntries
        objectName: "PresetList"

        delegate: QQC2.ItemDelegate {
            id: entry

            readonly property bool child: modelData.layerIndex >= 0
            required property int index
            required property var modelData

            bottomPadding: topPadding
            highlighted: modelData.sourceIndex === page.controller.selected && modelData.layerIndex
                         === page.controller.selectedLayer
            icon.name: child ? "" : modelData.id === page.controller.defaultId ? "emblem-default" :
                                                                                 "preferences-desktop-color"
            implicitHeight: Math.max(icon.height, presetLabel.implicitHeight, addLayer.implicitHeight)
                            + topPadding + bottomPadding
            leftPadding: padding
            objectName: "Preset" + modelData.sourceIndex + (child ? "Layer" + modelData.layerIndex : "")
            rightPadding: padding
            text: modelData.name
            topPadding: Kirigami.Units.smallSpacing
            width: ListView.view.width

            contentItem: RowLayout {
                spacing: entry.spacing

                Kirigami.Icon {
                    Layout.preferredHeight: entry.icon.height
                    Layout.preferredWidth: entry.icon.width
                    selected: entry.highlighted || entry.down
                    source: entry.icon.name
                }

                QQC2.Label {
                    id: presetLabel

                    Accessible.ignored: true
                    Layout.fillWidth: true
                    color: entry.highlighted || entry.down ? Kirigami.Theme.highlightedTextColor :
                                                             Kirigami.Theme.textColor
                    elide: Text.ElideRight
                    font: entry.font
                    objectName: "PresetLabel"
                    text: entry.text
                    textFormat: Text.PlainText
                    verticalAlignment: Text.AlignVCenter
                }

                QQC2.ToolButton {
                    id: addLayer

                    Accessible.name: qsTranslate("AmbientLed.LayerEditor", "Add Layer")
                    Layout.alignment: Qt.AlignVCenter
                    QQC2.ToolTip.text: Accessible.name
                    QQC2.ToolTip.visible: hovered
                    enabled: entry.modelData.layerCount < 32
                    icon.name: page.recorder.recording && page.recordingPreset === entry.modelData.id
                               ? "dialog-cancel" : "list-add"
                    objectName: "AddLayer" + entry.modelData.sourceIndex
                    visible: !entry.child

                    onClicked: page.recordLayer(entry.modelData.sourceIndex)
                }
            }

            onClicked: page.selectEntry(modelData.sourceIndex, modelData.layerIndex)

            QQC2.ToolTip {
                id: presetTip

                objectName: "PresetToolTip"
                text: entry.text
                visible: entry.hovered

                contentItem: Text {
                    color: presetTip.palette.toolTipText
                    font: presetTip.font
                    text: presetTip.text
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    Component.onCompleted: Qt.callLater(showEditor)

    Connections {
        function onAccepted(conditions) {
            if (!page.recordingPreset || page.recordingPreset !== page.controller.preset.id)
                return;
            page.recordingPreset = "";
            const added = page.controller.addLayer(conditions);
            if (added >= 0) {
                search.clear();
                page.controller.selectedLayer = added;
                page.showEditor();
            }
        }

        function onChanged() {
            if (!page.recorder.recording)
                page.recordingPreset = "";
        }

        target: page.recorder
    }

    FileDialog {
        id: importDialog

        nameFilters: [qsTranslate("AmbientLed.PresetsPage", "Lighting presets (*.json)")]
        title: qsTranslate("AmbientLed.PresetsPage", "Import Lighting Preset")

        onAccepted: {
            search.clear();
            page.controller.importFile(selectedFile);
            page.showEditor();
        }
    }
}
