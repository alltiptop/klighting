// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root

    readonly property var choices: [
        {
            name: qsTranslate("AmbientLed.StripEditor", "None (color only)"),
            ambient: false,
            id: ""
        },
        {
            name: qsTranslate("AmbientLed.StripEditor", "Default effect"),
            ambient: true,
            id: ""
        }
    ].concat(controller.effectPresets.filter(e => e.type === "ambient").map(e => ({
        name: e.name,
        ambient: true,
        id: e.id
    })))
    required property var controller
    property bool editingGradient: false
    property int gradientIndex: -1
    readonly property var gradientList: {
        revision;
        return controller.gradients(layerIndex);
    }
    required property var keyboard
    required property int layerIndex
    property string presetId: controller.preset.id
    property int revision: 0
    required property var strip

    function editGradient(index) {
        gradientIndex = index;
        const g = index < 0 ? null : gradientList[index];
        const selected = strip.elements.filter(e => keyboard.selection.indexOf(e.id) >= 0);
        from.currentIndex = g ? strip.elements.findIndex(e => e.id === g.from) : selected.length
                                ? strip.elements.findIndex(e => e.id === selected[0].id) : 0;
        to.currentIndex = g ? strip.elements.findIndex(e => e.id === g.to) : selected.length ? strip.elements.findIndex(
                                                                                                   e => e.id
                                                                                                        === selected[selected.length
                                                                                                                     - 1].id) :
                                                                                               strip.elements.length
                                                                                               - 1;
        start.text = g ? g.start : "#ff0000";
        end.text = g ? g.end : "#0000ff";
        opacity.value = g ? g.opacity : 100;
        effect.currentIndex = g ? choices.findIndex(e => e.ambient === g.ambient && e.id === g.effectId) : 0;
        editingGradient = true;
    }

    function labelFor(id) {
        return strip.elements.find(e => e.id === id)?.label || String(id);
    }

    objectName: "StripView" + strip.id

    onLayerIndexChanged: editingGradient = false
    onPresetIdChanged: {
        editingGradient = false;
        gradientIndex = -1;
    }

    Connections {
        function onContentChanged() {
            ++root.revision;
            if (root.gradientIndex >= root.gradientList.length)
                root.editingGradient = false;
        }

        target: root.controller
    }

    QQC2.Label {
        Layout.fillWidth: true
        text: qsTranslate("AmbientLed.StripEditor",
                          "Click an element; Ctrl-click adds elements, Shift-click selects a range.")
        visible: root.controller.device.perStrip
        wrapMode: Text.Wrap
    }

    Repeater {
        model: root.gradientList

        RowLayout {
            id: gradientRow

            required property int index
            required property var modelData

            Layout.fillWidth: true
            visible: modelData.strip === root.strip.id

            Rectangle {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 4
                implicitHeight: Kirigami.Units.gridUnit

                gradient: Gradient {
                    orientation: Gradient.Horizontal

                    GradientStop {
                        color: gradientRow.modelData.start
                        position: 0
                    }

                    GradientStop {
                        color: gradientRow.modelData.end
                        position: 1
                    }
                }
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: qsTranslate("AmbientLed.StripEditor", "%1 → %2").arg(root.labelFor(
                                                                               gradientRow.modelData.from)).arg(
                          root.labelFor(gradientRow.modelData.to))
            }

            QQC2.ToolButton {
                Accessible.name: qsTranslate("AmbientLed.StripEditor", "Edit Gradient")
                QQC2.ToolTip.text: Accessible.name
                QQC2.ToolTip.visible: hovered
                icon.name: "document-edit"
                objectName: "EditGradient" + gradientRow.index

                onClicked: root.editGradient(gradientRow.index)
            }

            QQC2.ToolButton {
                Accessible.name: qsTranslate("AmbientLed.StripEditor", "Remove Gradient")
                QQC2.ToolTip.text: Accessible.name
                QQC2.ToolTip.visible: hovered
                icon.name: "edit-delete"

                onClicked: {
                    root.editingGradient = false;
                    root.controller.removeGradient(root.layerIndex, gradientRow.index);
                }
            }
        }
    }

    Kirigami.FormLayout {
        Layout.fillWidth: true
        enabled: root.controller.editableLightbar
        visible: root.editingGradient

        QQC2.ComboBox {
            id: from

            Kirigami.FormData.label: qsTranslate("AmbientLed.StripEditor", "From element:")
            model: root.strip.elements
            objectName: "GradientFrom"
            textRole: "label"
            valueRole: "id"
        }

        QQC2.ComboBox {
            id: to

            Kirigami.FormData.label: qsTranslate("AmbientLed.StripEditor", "To element:")
            model: root.strip.elements
            objectName: "GradientTo"
            textRole: "label"
            valueRole: "id"
        }

        HexColor {
            id: start

            Kirigami.FormData.label: qsTranslate("AmbientLed.StripEditor", "Start color:")
            label: qsTranslate("AmbientLed.StripEditor", "Start color")
            objectName: "GradientStart"

            onEdited: color => start.text = color
        }

        HexColor {
            id: end

            Kirigami.FormData.label: qsTranslate("AmbientLed.StripEditor", "End color:")
            label: qsTranslate("AmbientLed.StripEditor", "End color")
            objectName: "GradientEnd"

            onEdited: color => end.text = color
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Kirigami.Units.gridUnit

            gradient: Gradient {
                orientation: Gradient.Horizontal

                GradientStop {
                    color: start.acceptableInput ? start.text : "transparent"
                    position: 0
                }

                GradientStop {
                    color: end.acceptableInput ? end.text : "transparent"
                    position: 1
                }
            }
        }

        QQC2.ComboBox {
            id: effect

            Kirigami.FormData.label: qsTranslate("AmbientLed.StripEditor", "Underlying effect:")
            Layout.fillWidth: true
            model: root.choices
            objectName: "GradientEffect"
            textRole: "name"
        }

        RowLayout {
            Kirigami.FormData.label: qsTranslate("AmbientLed.StripEditor", "Gradient opacity:")

            QQC2.Slider {
                Accessible.name: qsTranslate("AmbientLed.StripEditor", "Gradient opacity")
                Layout.fillWidth: true
                from: 0
                stepSize: 1
                to: 100
                value: opacity.value

                onMoved: opacity.value = value
            }

            QQC2.SpinBox {
                id: opacity

                editable: true
                from: 0
                objectName: "GradientOpacity"
                to: 100
                value: 100
            }

            QQC2.Label {
                text: "%"
            }
        }

        FormHint {
            text: qsTranslate("AmbientLed.StripEditor",
                              "The gradient replaces effects in this range. Lower opacity reveals the underlying effect.")
        }

        RowLayout {
            QQC2.Button {
                enabled: start.acceptableInput && end.acceptableInput && from.currentIndex >= 0
                         && to.currentIndex >= 0 && effect.currentIndex >= 0
                objectName: "SaveGradient"
                text: root.gradientIndex < 0 ? qsTranslate("AmbientLed.StripEditor", "Add Gradient") :
                                               qsTranslate("AmbientLed.StripEditor", "Update Gradient")

                onClicked: {
                    const choice = root.choices[effect.currentIndex];
                    if (root.controller.setGradient(root.layerIndex, root.gradientIndex, root.strip.id, from.currentValue,
                                                    to.currentValue, start.text, end.text, opacity.value,
                                                    choice.ambient, choice.id))
                        root.editingGradient = false;
                }
            }

            QQC2.Button {
                text: qsTranslate("AmbientLed.StripEditor", "Cancel")

                onClicked: root.editingGradient = false
            }
        }
    }
}
