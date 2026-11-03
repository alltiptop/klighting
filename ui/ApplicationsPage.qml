// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

SidebarPage {
    id: page

    readonly property var filteredEntries: controller.rules.map((entry, index) => {
        return Object.assign({}, entry, {
                                 "sourceIndex": index
                             });
    }).filter(entry => {
        return [entry.name, entry.windowClass].join(" ").toLocaleLowerCase().includes(search.text.trim().toLocaleLowerCase(
                                                                                          ));
    })
    property int selectedRule: controller.rules.length > 0 ? 0 : -1

    function showEditor() {
        if (navigator.depth < navigationLevel + 2)
            open("RulePage.qml", {
                     "selection": page
                 });
        else
            navigator.currentIndex = navigationLevel + 1;
    }

    extraFooterTopPadding: true
    navigationLevel: 1
    objectName: "ApplicationSidebar"
    title: qsTranslate("AmbientLed.ApplicationsPage", "Applications")

    footer: QQC2.Button {
        enabled: page.controller.rules.length < 128
        icon.name: "list-add"
        objectName: "AddApplication"
        text: qsTranslate("AmbientLed.ApplicationsPage", "Add Application")

        onClicked: {
            const index = page.controller.addRule();
            if (index >= 0) {
                page.selectedRule = index;
                search.clear();
                page.showEditor();
            }
        }
    }
    header: ColumnLayout {
        Kirigami.SearchField {
            id: search

            Accessible.name: qsTranslate("AmbientLed.ApplicationsPage", "Filter applications")
            Layout.fillWidth: true
            objectName: "ApplicationSearch"
            placeholderText: qsTranslate("AmbientLed.ApplicationsPage", "Search…")
        }
    }
    view: ListView {
        currentIndex: page.filteredEntries.findIndex(entry => {
            return entry.sourceIndex === page.selectedRule;
        })
        model: page.filteredEntries
        objectName: "ApplicationList"

        delegate: QQC2.ItemDelegate {
            required property int index
            required property var modelData

            highlighted: modelData.sourceIndex === page.selectedRule
            icon.name: modelData.icon.startsWith("/") ? "" : modelData.icon
            icon.source: modelData.icon.startsWith("/") ? "file://" + modelData.icon : ""
            objectName: "Application" + modelData.sourceIndex
            opacity: modelData.enabled ? 1 : 0.6
            text: modelData.name || modelData.windowClass || qsTranslate("AmbientLed.ApplicationsPage",
                                                                         "New Application")
            width: ListView.view.width

            onClicked: {
                page.selectedRule = modelData.sourceIndex;
                page.showEditor();
            }
        }
    }

    Component.onCompleted: Qt.callLater(showEditor)

    Connections {
        function onContentChanged() {
            if (page.selectedRule >= page.controller.rules.length)
                page.selectedRule = page.controller.rules.length - 1;
        }

        target: page.controller
    }
}
