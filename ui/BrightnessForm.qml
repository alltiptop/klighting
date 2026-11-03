// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.FormLayout {
    id: form

    required property var options

    signal edited(int value)

    RowLayout {
        Kirigami.FormData.label: qsTranslate("AmbientLed.BrightnessForm", "Brightness:")

        QQC2.Slider {
            Accessible.name: qsTranslate("AmbientLed.BrightnessForm", "Brightness")
            Layout.fillWidth: true
            Layout.preferredWidth: Kirigami.Units.gridUnit * 12
            from: 0
            objectName: "BrightnessSlider"
            stepSize: 1
            to: 100
            value: form.options.brightness ?? 100

            onMoved: form.edited(Math.round(value))
        }

        QQC2.SpinBox {
            Accessible.name: qsTranslate("AmbientLed.BrightnessForm", "Brightness in percent")
            editable: true
            from: 0
            objectName: "BrightnessValue"
            to: 100
            value: form.options.brightness ?? 100

            onValueModified: form.edited(value)
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
