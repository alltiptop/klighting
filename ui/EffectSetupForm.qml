// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root

    property bool allowAmbient: true
    property int brightnessSteps: 0
    required property var controller
    required property var options

    signal edited(string group, string key, var value)

    TriggerForm {
        Layout.fillWidth: true
        controller: root.controller
        options: root.options.condition || ({})
        visible: root.options.type !== "ambient"

        onEdited: (key, value) => root.edited("trigger", key, value)
    }

    Kirigami.FormLayout {
        Layout.fillWidth: true

        QQC2.ComboBox {
            property var types: root.allowAmbient ? ["solid", "brightness", "pulse", "ambient"] : ["solid", "brightness",
                                                                                                   "pulse"]

            Kirigami.FormData.label: qsTranslate("AmbientLed.EffectSetupForm", "Effect:")
            currentIndex: types.indexOf(root.options.type)
            enabled: root.options.type !== "ambient"
            model: root.allowAmbient ? [qsTranslate("AmbientLed.EffectSetupForm", "Solid Color"), qsTranslate(
                                            "AmbientLed.EffectSetupForm", "Brightness"), qsTranslate(
                                            "AmbientLed.EffectSetupForm", "Pulse"), "Ambient"] : [qsTranslate(
                                                                                                      "AmbientLed.EffectSetupForm",
                                                                                                      "Solid Color"),
                                                                                                  qsTranslate(
                                                                                                      "AmbientLed.EffectSetupForm",
                                                                                                      "Brightness"),
                                                                                                  qsTranslate(
                                                                                                      "AmbientLed.EffectSetupForm",
                                                                                                      "Pulse")]
            objectName: "EffectType"

            onActivated: root.edited("effect-type", "type", types[currentIndex])
        }

        HexColor {
            Kirigami.FormData.label: qsTranslate("AmbientLed.SolidPage", "Color:")
            label: qsTranslate("AmbientLed.SolidPage", "Solid color")
            text: root.options.solidColor || "#ffffff"
            visible: root.options.type === "solid"

            onEdited: value => root.edited("solid", "color", value)
        }
    }

    Loader {
        Layout.fillWidth: true
        active: !!root.controller && !!root.options.id && root.options.type !== "solid"
        sourceComponent: root.options.type === "pulse" ? pulse : root.options.type === "brightness"
                                                         ? brightness : ambient
        visible: active
    }

    Component {
        id: pulse

        PulseForm {
            options: root.options

            onEdited: (key, value) => root.edited("notifications", key, value)
        }
    }

    Component {
        id: brightness

        BrightnessForm {
            brightnessSteps: root.brightnessSteps
            options: root.options

            onEdited: value => root.edited("brightness", "brightness", value)
        }
    }

    Component {
        id: ambient

        AmbientForm {
            controller: root.controller
            options: root.options

            onEdited: (key, value) => root.edited("ambient", key, value)
        }
    }

    Loader {
        Layout.fillWidth: true
        active: root.options.type === "solid" || root.options.type === "pulse"
        visible: active

        sourceComponent: EffectPowerForm {
            options: root.options

            onEdited: (key, value) => root.edited("power", key, value)
        }
    }
}
