// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

SettingsPage {
    id: page

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
    title: controller.deviceSettings.name || qsTranslate("AmbientLed.LightingPage", "Devices")

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.Heading {
            Layout.fillWidth: true
            level: 2
            text: page.controller.deviceSettings.name || qsTranslate("AmbientLed.LightingPage",
                                                                     "Add a lighting device")
            wrapMode: Text.Wrap
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            text: page.controller.device.error
            type: Kirigami.MessageType.Warning
            visible: text.length > 0
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true

            QQC2.CheckBox {
                checked: page.controller.effect.enabled
                enabled: page.controller.effect.available && !page.controller.effect.busy
                objectName: "EnableEffect"
                text: qsTranslate("AmbientLed.LightingPage", "Enable Ambient LED effect")

                onToggled: page.controller.effect.enabled = checked
            }

            FormHint {
                text: page.controller.effect.status
            }

            Item {
                Kirigami.FormData.isSection: true
            }

            FormHint {
                text: qsTranslate("AmbientLed.LightingPage",
                                  "Lighting will resume when this device reconnects.")
                visible: page.controller.selectedDevice >= 0 && !page.controller.device.id
            }

            QQC2.CheckBox {
                checked: page.controller.deviceSettings.enabled ?? false
                objectName: "EnableDevice"
                text: qsTranslate("AmbientLed.LightingPage", "Enable this device")
                visible: page.controller.selectedDevice >= 0

                onToggled: page.controller.setDevice("enabled", checked)
            }

            QQC2.TextField {
                Kirigami.FormData.label: qsTranslate("AmbientLed.LightingPage", "Name:")
                Layout.maximumWidth: Kirigami.Units.gridUnit * 20
                Layout.preferredWidth: Kirigami.Units.gridUnit * 20
                maximumLength: 128
                objectName: "DeviceName"
                text: page.controller.deviceSettings.name || ""
                visible: page.controller.selectedDevice >= 0

                onTextEdited: page.controller.setDevice("name", text)
            }

            QQC2.ComboBox {
                Kirigami.FormData.label: qsTranslate("AmbientLed.LightingPage", "Preset:")
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 20
                Layout.preferredWidth: Kirigami.Units.gridUnit * 20
                currentIndex: model.findIndex(p => {
                    return p.id === page.controller.deviceSettings.presetId;
                })
                model: [
                    {
                        "id": "",
                        "name": qsTranslate("AmbientLed.LightingPage", "Default for this device")
                    }
                ].concat(page.controller.presets.filter(p => {
                    return p.deviceId === page.controller.deviceSettings.model;
                }))
                objectName: "DevicePreset"
                textRole: "name"
                valueRole: "id"
                visible: page.controller.selectedDevice >= 0

                onActivated: page.controller.setDevice("presetId", currentValue)
            }

            QQC2.CheckBox {
                checked: page.controller.deviceSettings.followApplications ?? false
                objectName: "DeviceApplications"
                text: qsTranslate("AmbientLed.LightingPage", "Follow application bindings")
                visible: page.controller.selectedDevice >= 0

                onToggled: page.controller.setDevice("followApplications", checked)
            }

            QQC2.CheckBox {
                checked: page.controller.lightbarEnabled
                enabled: page.controller.lightbarAvailable && !page.controller.busy
                objectName: "EnableLightbar"
                text: qsTranslate("AmbientLed.LightingPage", "Enable lightbar")
                visible: page.controller.device.lightbarPower

                onToggled: page.controller.lightbarEnabled = checked
            }

            FormHint {
                text: page.controller.lightbarStatus
                visible: text.length > 0
            }

            Item {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: qsTranslate("AmbientLed.LightingPage", "Power saving")
                visible: page.controller.selectedDevice >= 0
            }

            QQC2.CheckBox {
                checked: page.controller.deviceSettings.lightbarOffOnBattery ?? false
                objectName: "LightbarOffOnBattery"
                text: qsTranslate("AmbientLed.LightingPage", "Turn off lightbar on battery power")
                visible: page.controller.device.lightbar

                onToggled: page.controller.setDevice("lightbarOffOnBattery", checked)
            }

            QQC2.CheckBox {
                checked: page.controller.deviceSettings.lightbarOffOnLowBattery ?? false
                enabled: !(page.controller.deviceSettings.lightbarOffOnBattery ?? false)
                objectName: "LightbarOffOnLowBattery"
                text: qsTranslate("AmbientLed.LightingPage", "Turn off lightbar on low battery")
                visible: page.controller.device.lightbar

                onToggled: page.controller.setDevice("lightbarOffOnLowBattery", checked)
            }

            QQC2.CheckBox {
                checked: page.controller.deviceSettings.pauseOnBattery ?? false
                objectName: "PauseOnBattery"
                text: qsTranslate("AmbientLed.LightingPage", "Pause all effects on battery power")
                visible: page.controller.selectedDevice >= 0

                onToggled: page.controller.setDevice("pauseOnBattery", checked)
            }

            QQC2.CheckBox {
                checked: page.controller.deviceSettings.pauseOnLowBattery ?? false
                enabled: !(page.controller.deviceSettings.pauseOnBattery ?? false)
                objectName: "PauseOnLowBattery"
                text: qsTranslate("AmbientLed.LightingPage", "Pause all effects on low battery")
                visible: page.controller.selectedDevice >= 0

                onToggled: page.controller.setDevice("pauseOnLowBattery", checked)
            }

            FormHint {
                text: qsTranslate("AmbientLed.LightingPage",
                                  "Pausing freezes colors and stops capture and notifications. Lightbar rules are independent. Low battery uses KDE’s threshold.")
                visible: page.controller.selectedDevice >= 0
            }

            Item {
                Kirigami.FormData.isSection: true
                visible: page.controller.selectedDevice >= 0
            }

            QQC2.Label {
                Kirigami.FormData.label: qsTranslate("AmbientLed.LightingPage", "Driver:")
                text: page.controller.deviceSettings.driver || ""
                visible: page.controller.selectedDevice >= 0
            }

            QQC2.Label {
                Kirigami.FormData.label: qsTranslate("AmbientLed.LightingPage", "Device ID:")
                Layout.fillWidth: true
                text: page.controller.deviceSettings.instance || ""
                visible: page.controller.selectedDevice >= 0
                wrapMode: Text.WrapAnywhere
            }

            QQC2.Button {
                enabled: !page.controller.busy
                icon.name: "list-remove"
                objectName: "RemoveDevice"
                text: qsTranslate("AmbientLed.LightingPage", "Remove Device")
                visible: page.controller.selectedDevice >= 0

                onClicked: page.controller.removeDevice()
            }

            FormHint {
                text: qsTranslate("AmbientLed.LightingPage", "Removing a device keeps its presets.")
                visible: page.controller.selectedDevice >= 0
            }

            FormHint {
                text: qsTranslate("AmbientLed.LightingPage", "Add a device to configure its lighting.")
                visible: page.controller.selectedDevice < 0
            }
        }
    }
}
