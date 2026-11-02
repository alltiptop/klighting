// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Kirigami.FormLayout {
    id: form

    required property var options

    signal edited(string key, bool value)

    Item {
        Kirigami.FormData.isSection: true
        Kirigami.FormData.label: qsTranslate("AmbientLed.EffectPowerForm", "Power saving")
    }

    QQC2.CheckBox {
        checked: form.options.pauseOnBattery ?? false
        objectName: "EffectPauseOnBattery"
        text: qsTranslate("AmbientLed.EffectPowerForm", "Pause on battery power")

        onToggled: form.edited("pauseOnBattery", checked)
    }

    QQC2.CheckBox {
        checked: form.options.pauseOnLowBattery ?? false
        enabled: !(form.options.pauseOnBattery ?? false)
        objectName: "EffectPauseOnLowBattery"
        text: qsTranslate("AmbientLed.EffectPowerForm", "Pause on low battery")

        onToggled: form.edited("pauseOnLowBattery", checked)
    }

    FormHint {
        text: form.options.type === "ambient" ? qsTranslate("AmbientLed.EffectPowerForm",
                                                            "Pausing stops screen capture and keeps the last color. Low battery uses KDE’s threshold.") :
                                                qsTranslate("AmbientLed.EffectPowerForm",
                                                            "Only this effect is paused. Low battery uses KDE’s threshold.")
    }
}
