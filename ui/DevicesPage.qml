// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

SidebarPage {
    id: page

    function showEditor() {
        if (navigator.depth < navigationLevel + 2)
            open("LightingPage.qml");
        else
            navigator.currentIndex = navigationLevel + 1;
    }

    navigationLevel: 1
    objectName: "DeviceSidebar"
    title: qsTranslate("AmbientLed.DevicesPage", "Devices")

    footer: RowLayout {
        QQC2.Button {
            enabled: page.controller.devices.length < 32
            icon.name: "list-add"
            objectName: "AddDevice"
            text: qsTranslate("AmbientLed.DevicesPage", "Add…")

            onClicked: {
                page.controller.refreshDevices();
                addMenu.popup();
            }

            QQC2.Menu {
                id: addMenu

                Instantiator {
                    model: page.controller.availableDevices

                    delegate: QQC2.MenuItem {
                        required property var modelData

                        text: modelData.name + " · " + modelData.instance

                        onTriggered: {
                            if (page.controller.addDevice(modelData.id))
                                page.showEditor();
                        }
                    }

                    onObjectAdded: (index, object) => {
                        return addMenu.insertItem(index, object);
                    }
                    onObjectRemoved: (index, object) => {
                        return addMenu.removeItem(object);
                    }
                }

                QQC2.MenuItem {
                    enabled: false
                    text: qsTranslate("AmbientLed.DevicesPage", "No new supported devices found")
                    visible: page.controller.availableDevices.length === 0
                }
            }
        }

        QQC2.ToolButton {
            Accessible.name: qsTranslate("AmbientLed.DevicesPage", "Refresh Devices")
            QQC2.ToolTip.text: Accessible.name
            QQC2.ToolTip.visible: hovered
            icon.name: "view-refresh"
            objectName: "RefreshDevices"

            onClicked: page.controller.refreshDevices()
        }
    }
    view: ListView {
        currentIndex: page.controller.selectedDevice
        model: page.controller.devices

        delegate: QQC2.ItemDelegate {
            required property int index
            required property var modelData

            highlighted: index === page.controller.selectedDevice
            icon.name: modelData.connected ? "input-keyboard" : "network-disconnect"
            objectName: "Device" + index
            text: modelData.name + (!modelData.connected ? qsTranslate("AmbientLed.DevicesPage",
                                                                       " (unavailable)") : !modelData.enabled
                                                           ? qsTranslate("AmbientLed.DevicesPage",
                                                                         " (disabled)") : "")
            width: ListView.view.width

            onClicked: {
                page.recorder.cancel();
                page.controller.selectedDevice = index;
                page.showEditor();
            }
        }
    }

    Component.onCompleted: Qt.callLater(showEditor)
}
