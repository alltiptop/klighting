// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.FormLayout {
    id: form

    property int brightnessSteps: 0
    required property var options
    readonly property int sliderSteps: brightnessSteps > 0 && brightnessSteps < 100 ? brightnessSteps : 100

    signal edited(int value)

    RowLayout {
        Kirigami.FormData.label: qsTranslate("AmbientLed.BrightnessForm", "Brightness:")

        QQC2.Slider {
            Accessible.name: qsTranslate("AmbientLed.BrightnessForm", "Brightness")
            Layout.fillWidth: true
            Layout.preferredWidth: Kirigami.Units.gridUnit * 12
            from: 0
            objectName: "BrightnessSlider"
            snapMode: QQC2.Slider.SnapAlways
            stepSize: 1
            to: form.sliderSteps
            value: Math.round((form.options.brightness ?? 100) * form.sliderSteps / 100)

            onMoved: form.edited(Math.round(value * 100 / form.sliderSteps))
        }

        QQC2.SpinBox {
            id: brightnessInput

            Accessible.name: qsTranslate("AmbientLed.BrightnessForm", "Brightness in percent")
            editable: true
            from: 0
            objectName: "BrightnessValue"
            textFromValue: (value, locale) => Number(Math.round(value * 100
                                                                / form.sliderSteps)).toLocaleString(locale,
                                                                                                    'f', 0)
            to: form.sliderSteps
            value: Math.round((form.options.brightness ?? 100) * form.sliderSteps / 100)
            valueFromText: (text, locale) => Math.round(Number.fromLocaleString(locale, text)
                                                        * form.sliderSteps / 100)

            validator: IntValidator {
                bottom: 0
                locale: brightnessInput.locale.name
                top: 100
            }

            onValueModified: form.edited(Math.round(value * 100 / form.sliderSteps))
        }

        QQC2.Label {
            text: "%"
        }
    }

    FormHint {
        text: qsTranslate("AmbientLed.BrightnessForm",
                          "Scales colors from earlier effects in the list. 100% keeps their brightness.")
    }
}
