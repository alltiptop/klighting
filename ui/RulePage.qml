// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

SettingsPage {
    id: page

    property var current: controller.rules[rule] || {
                              name: "",
                              windowClass: "",
                              presetId: "",
                              enabled: false
                          }
    readonly property int rule: selection.selectedRule
    required property var selection

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
    objectName: "ApplicationEditor"
    title: qsTranslate("AmbientLed.RulePage", "Application Binding")

    actions: [
        Kirigami.Action {
            enabled: page.rule > 0
            icon.name: "go-up"
            text: qsTranslate("AmbientLed.RulePage", "Move Up")

            onTriggered: page.selection.selectedRule = page.controller.moveRule(page.rule, -1)
        },
        Kirigami.Action {
            enabled: page.rule >= 0 && page.rule + 1 < page.controller.rules.length
            icon.name: "go-down"
            text: qsTranslate("AmbientLed.RulePage", "Move Down")

            onTriggered: page.selection.selectedRule = page.controller.moveRule(page.rule, 1)
        },
        Kirigami.Action {
            enabled: page.rule >= 0
            icon.name: "edit-delete"
            text: qsTranslate("AmbientLed.RulePage", "Remove")

            onTriggered: {
                page.controller.removeRule(page.rule);
                page.selection.selectedRule = Math.min(page.rule, page.controller.rules.length - 1);
            }
        }
    ]

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.PlaceholderMessage {
            Layout.fillWidth: true
            explanation: qsTranslate("AmbientLed.RulePage",
                                     "Add an application to use a different lighting preset while its window is active.")
            text: qsTranslate("AmbientLed.RulePage", "No application bindings")
            visible: page.rule < 0
        }

        QQC2.Label {
            Layout.fillWidth: true
            text: qsTranslate("AmbientLed.RulePage",
                              "The first enabled rule with an exact application class match wins. The desktop and unmatched windows use the default preset.")
            wrapMode: Text.Wrap
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true
            visible: page.rule >= 0

            QQC2.CheckBox {
                checked: page.current.enabled
                text: qsTranslate("AmbientLed.RulePage", "Enable this binding")

                onToggled: page.controller.setRule(page.rule, "enabled", checked)
            }

            QQC2.TextField {
                Kirigami.FormData.label: qsTranslate("AmbientLed.RulePage", "Application:")
                Layout.maximumWidth: Kirigami.Units.gridUnit * 20
                Layout.preferredWidth: Kirigami.Units.gridUnit * 20
                maximumLength: 128
                text: page.current.name

                onTextEdited: page.controller.setRule(page.rule, "name", text)
            }

            QQC2.TextField {
                Kirigami.FormData.label: qsTranslate("AmbientLed.RulePage", "Window Class (Application):")
                Layout.maximumWidth: Kirigami.Units.gridUnit * 20
                Layout.preferredWidth: Kirigami.Units.gridUnit * 20
                maximumLength: 512
                objectName: "WindowClass"
                text: page.current.windowClass

                onTextEdited: page.controller.setRule(page.rule, "windowClass", text)
            }

            QQC2.Label {
                text: qsTranslate("AmbientLed.RulePage", "Exact match")
            }

            QQC2.Button {
                enabled: !page.controller.pickingWindow
                icon.name: "edit-find"
                objectName: "SelectActiveWindow"
                text: qsTranslate("AmbientLed.RulePage", "Select Active Window")

                onClicked: page.controller.selectWindow(page.rule)
            }

            FormHint {
                text: page.controller.windowStatus
                visible: text.length > 0
            }

            QQC2.ComboBox {
                Kirigami.FormData.label: qsTranslate("AmbientLed.RulePage", "Preset:")
                Layout.maximumWidth: Kirigami.Units.gridUnit * 20
                Layout.preferredWidth: Kirigami.Units.gridUnit * 20
                currentIndex: page.controller.presets.findIndex(p => p.id === page.current.presetId)
                model: page.controller.presets
                textRole: "name"
                valueRole: "id"

                onActivated: page.controller.setRule(page.rule, "presetId", currentValue)
            }
        }
    }
}
