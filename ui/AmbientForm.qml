// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: form

    required property var controller
    required property var options

    signal edited(string key, var value)

    spacing: Kirigami.Units.largeSpacing

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        text: qsTranslate("AmbientLed.AmbientForm", "Enable Ambient LED in Devices to use this preset.")
        type: Kirigami.MessageType.Information
        visible: !form.controller.effect.loaded
    }

    Kirigami.FormLayout {
        Layout.fillWidth: true

        QQC2.SpinBox {
            Kirigami.FormData.label: qsTranslate("AmbientLed.AmbientForm", "Screen sampling rate (FPS):")
            editable: true
            from: 1
            objectName: "FramesPerSecond"
            to: 20
            value: form.options.fps

            onValueModified: form.edited("fps", value)
        }

        QQC2.SpinBox {
            Kirigami.FormData.label: qsTranslate("AmbientLed.AmbientForm", "Color update rate (FPS):")
            editable: true
            from: 1
            objectName: "LedFramesPerSecond"
            to: 60
            value: form.options.ledFps

            onValueModified: form.edited("ledFps", value)
        }

        QQC2.SpinBox {
            Kirigami.FormData.label: qsTranslate("AmbientLed.AmbientForm", "Color smoothing (ms):")
            editable: true
            from: 0
            objectName: "SmoothingMs"
            stepSize: 50
            to: 3000
            value: form.options.smoothing

            onValueModified: form.edited("smoothing", value)
        }

        FormHint {
            text: qsTranslate("AmbientLed.AmbientForm",
                              "Sample at 2–3 FPS and use a higher update rate for smooth transitions.")
        }

        Item {
            Kirigami.FormData.isSection: true
        }

        QQC2.SpinBox {
            Kirigami.FormData.label: qsTranslate("AmbientLed.AmbientForm", "Minimum brightness (%):")
            editable: true
            from: 0
            objectName: "MinimumBrightness"
            to: 100
            value: form.options.minimum

            onValueModified: form.edited("minimum", value)
        }

        QQC2.SpinBox {
            Kirigami.FormData.label: qsTranslate("AmbientLed.AmbientForm", "Maximum brightness (%):")
            editable: true
            from: 0
            objectName: "MaximumBrightness"
            to: 100
            value: form.options.maximum

            onValueModified: form.edited("maximum", value)
        }

        FormHint {
            text: qsTranslate("AmbientLed.AmbientForm",
                              "A minimum above zero keeps lighting visible on dark screens. Brightness is relative to the hardware setting.")
        }

        Item {
            Kirigami.FormData.isSection: true
        }

        ColumnLayout {
            Kirigami.FormData.label: qsTranslate("AmbientLed.AmbientForm", "Color temperature:")
            Layout.fillWidth: true
            enabled: form.controller.device.color

            RowLayout {
                QQC2.Label {
                    text: qsTranslate("AmbientLed.AmbientForm", "Warm")
                }

                QQC2.Slider {
                    Accessible.name: qsTranslate("AmbientLed.AmbientForm", "Color temperature")
                    Layout.fillWidth: true
                    Layout.minimumWidth: Kirigami.Units.gridUnit * 8
                    from: -100
                    objectName: "ColorTemperature"
                    stepSize: 1
                    to: 100
                    value: form.options.temperature

                    onMoved: form.edited("temperature", Math.round(value))
                }

                QQC2.Label {
                    text: qsTranslate("AmbientLed.AmbientForm", "Cool")
                }
            }

            QQC2.Label {
                text: form.options.temperature === 0 ? qsTranslate("AmbientLed.AmbientForm", "Neutral") :
                                                       form.options.temperature < 0 ? qsTranslate(
                                                                                          "AmbientLed.AmbientForm",
                                                                                          "Warm %1%").arg(
                                                                                          -form.options.temperature) :
                                                                                      qsTranslate(
                                                                                          "AmbientLed.AmbientForm",
                                                                                          "Cool %1%").arg(
                                                                                          form.options.temperature)
            }
        }
    }

    EffectPowerForm {
        Layout.fillWidth: true
        options: form.options

        onEdited: (key, value) => {
            return form.edited(key, value);
        }
    }
}
