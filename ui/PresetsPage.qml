// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

SidebarPage {
    id: page

    readonly property var filteredEntries: controller.presets.map((entry, index) => {
        return Object.assign({}, entry, {
                                 "sourceIndex": index
                             });
    }).filter(entry => {
        return [entry.name].join(" ").toLocaleLowerCase().includes(search.text.trim().toLocaleLowerCase());
    })

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
            return entry.sourceIndex === page.controller.selected;
        })
        model: page.filteredEntries
        objectName: "PresetList"

        delegate: QQC2.ItemDelegate {
            required property int index
            required property var modelData

            highlighted: modelData.sourceIndex === page.controller.selected
            icon.name: modelData.id === page.controller.defaultId ? "emblem-default" :
                                                                    "preferences-desktop-color"
            objectName: "Preset" + modelData.sourceIndex
            text: modelData.name
            width: ListView.view.width

            onClicked: {
                page.recorder.cancel();
                page.controller.selected = modelData.sourceIndex;
                page.showEditor();
            }
        }
    }

    Component.onCompleted: Qt.callLater(showEditor)

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
