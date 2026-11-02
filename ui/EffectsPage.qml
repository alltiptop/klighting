// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

SidebarPage {
    id: page

    readonly property var filteredEntries: controller.effectPresets.map((entry, index) => {
        return Object.assign({}, entry, {
                                 "sourceIndex": index
                             });
    }).filter(entry => {
        return [entry.name].join(" ").toLocaleLowerCase().includes(search.text.trim().toLocaleLowerCase());
    })

    function showEditor() {
        if (navigator.depth < navigationLevel + 2)
            open("EffectPage.qml");
        else
            navigator.currentIndex = navigationLevel + 1;
    }

    extraFooterTopPadding: true
    navigationLevel: 1
    objectName: "EffectSidebar"
    title: qsTranslate("AmbientLed.EffectsPage", "Effects")

    footer: RowLayout {
        QQC2.Button {
            enabled: page.controller.effectPresets.length < 64
            icon.name: "list-add"
            objectName: "AddEffect"
            text: qsTranslate("AmbientLed.EffectsPage", "Add…")

            onClicked: {
                search.clear();
                addEffectMenu.popup();
            }

            QQC2.Menu {
                id: addEffectMenu

                QQC2.MenuItem {
                    icon.name: "preferences-desktop-color"
                    objectName: "AddSolidEffect"
                    text: qsTranslate("AmbientLed.EffectsPage", "Solid Color")

                    onTriggered: {
                        if (page.controller.addEffect("solid", false)) {
                            page.showEditor();
                        }
                    }
                }

                QQC2.MenuItem {
                    icon.name: "notifications"
                    objectName: "AddNotificationsEffect"
                    text: qsTranslate("AmbientLed.EffectsPage", "Notifications")

                    onTriggered: {
                        if (page.controller.addEffect("notifications", false))
                            page.showEditor();
                    }
                }

                QQC2.MenuItem {
                    icon.name: "video-display"
                    objectName: "AddAmbientEffect"
                    text: qsTranslate("AmbientLed.EffectsPage", "Ambient")

                    onTriggered: {
                        if (page.controller.addEffect("ambient", false))
                            page.showEditor();
                    }
                }
            }
        }

        QQC2.ToolButton {
            Accessible.name: qsTranslate("AmbientLed.EffectsPage", "Import Effect Preset…")
            QQC2.ToolTip.text: Accessible.name
            QQC2.ToolTip.visible: hovered
            enabled: page.controller.effectPresets.length < 64
            icon.name: "document-import"

            onClicked: importDialog.open()
        }
    }
    header: ColumnLayout {
        Kirigami.SearchField {
            id: search

            Accessible.name: qsTranslate("AmbientLed.EffectsPage", "Filter effects")
            Layout.fillWidth: true
            objectName: "EffectSearch"
            placeholderText: qsTranslate("AmbientLed.EffectsPage", "Search…")
        }
    }
    view: ListView {
        id: presets

        currentIndex: page.filteredEntries.findIndex(entry => {
            return entry.sourceIndex === page.controller.selectedEffect;
        })
        model: page.filteredEntries
        objectName: "EffectList"

        delegate: QQC2.ItemDelegate {
            id: effectDelegate

            required property int index
            required property var modelData

            highlighted: modelData.sourceIndex === page.controller.selectedEffect
            objectName: "Effect" + modelData.sourceIndex
            text: modelData.name
            width: ListView.view.width

            contentItem: RowLayout {
                spacing: effectDelegate.spacing

                EffectIcon {
                    color: effectDelegate.modelData.solidColor || "#ffffff"
                    type: effectDelegate.modelData.type
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    color: effectDelegate.highlighted ? effectDelegate.palette.highlightedText :
                                                        effectDelegate.palette.text
                    elide: Text.ElideRight
                    text: effectDelegate.text
                    textFormat: Text.PlainText
                }
            }

            onClicked: {
                page.recorder.cancel();
                page.controller.selectedEffect = modelData.sourceIndex;
                page.showEditor();
            }
        }
    }

    Component.onCompleted: Qt.callLater(showEditor)

    FileDialog {
        id: importDialog

        nameFilters: [qsTranslate("AmbientLed.EffectsPage", "Effect presets (*.json)")]
        title: qsTranslate("AmbientLed.EffectsPage", "Import Effect Preset")

        onAccepted: {
            search.clear();
            page.controller.importEffectFile(selectedFile);
            page.showEditor();
        }
    }
}
