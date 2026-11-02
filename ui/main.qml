// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kcmutils as KCMUtils
import org.kde.kirigami as Kirigami

SidebarPage {
    id: page

    property int section: 0

    function selectSection(index) {
        controller.section = ["base", "keyboard", "effects", "applications"][index];
        if (section === index && navigator.depth > 1) {
            navigator.currentIndex = 1;
            return;
        }
        section = index;
        open(["DevicesPage.qml", "PresetsPage.qml", "EffectsPage.qml", "ApplicationsPage.qml"][index]);
    }

    KCMUtils.ConfigModule.columnWidth: Kirigami.Units.gridUnit * 12
    // The KCM host injects this context object.
    // qmllint disable unqualified
    controller: kcm.controller
    navigator: kcm
    objectName: "SectionSidebar"
    recorder: kcm.recorder
    // qmllint enable unqualified
    title: qsTranslate("AmbientLed.main", "Lighting")

    view: ListView {
        id: sections

        currentIndex: page.section
        model: [
            {
                name: qsTranslate("AmbientLed.main", "Devices"),
                icon: "preferences-devices"
            },
            {
                name: qsTranslate("AmbientLed.main", "Presets"),
                icon: "input-keyboard"
            },
            {
                name: qsTranslate("AmbientLed.main", "Effects"),
                icon: "video-display"
            },
            {
                name: qsTranslate("AmbientLed.main", "Applications"),
                icon: "preferences-system-windows"
            }
        ]

        delegate: QQC2.ItemDelegate {
            required property int index
            required property var modelData

            highlighted: page.section === index
            icon.name: modelData.icon
            objectName: "Section" + index
            text: modelData.name
            width: ListView.view.width

            onClicked: page.selectSection(index)
        }
    }

    Component.onCompleted: Qt.callLater(() => selectSection(0))
}
