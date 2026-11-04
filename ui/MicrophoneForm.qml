// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.FormLayout {
    id: form

    required property var controller
    readonly property var microphone: controller.service.microphoneStatus
    required property var options

    signal edited(string key, var value)

    HexColor {
        Kirigami.FormData.label: qsTranslate("AmbientLed.MicrophoneForm", "Color:")
        buttonObjectName: "ChooseMicrophoneColor"
        dialogObjectName: "MicrophoneColorDialog"
        fieldObjectName: "MicrophoneColor"
        label: qsTranslate("AmbientLed.MicrophoneForm", "Microphone color")
        text: form.options.microphoneColor || "#ff0000"

        onEdited: color => form.edited("color", color)
    }

    FormHint {
        text: qsTranslate("AmbientLed.MicrophoneForm",
                          "Shows this color when the system default input is unmuted, even when no application records. Muted or unknown state is transparent; this is not a privacy guarantee.")
    }

    FormHint {
        text: qsTranslate("AmbientLed.MicrophoneForm",
                          "Microphone indication does not require Ambient screen capture.")
    }

    FormHint {
        objectName: "MicrophoneStatus"
        text: !form.microphone.available ? qsTranslate("AmbientLed.MicrophoneForm",
                                                       "Microphone status unavailable") :
                                           !form.microphone.muteKnown ? qsTranslate(
                                                                            "AmbientLed.MicrophoneForm",
                                                                            "Microphone mute state unknown") :
                                                                        form.microphone.muted ? qsTranslate(
                                                                                                    "AmbientLed.MicrophoneForm",
                                                                                                    "Microphone muted") :
                                                                                                qsTranslate(
                                                                                                    "AmbientLed.MicrophoneForm",
                                                                                                    "Microphone unmuted")
    }

    FormHint {
        text: qsTranslate("AmbientLed.MicrophoneForm", "Default input: %1").arg(form.microphone.description
                                                                                || form.microphone.sourceName
                                                                                || "")
        visible: !!form.microphone.description || !!form.microphone.sourceName
    }

    FormHint {
        text: qsTranslate("AmbientLed.MicrophoneForm", "Backend: %1").arg(form.microphone.backend || "")
        visible: !!form.microphone.backend
    }

    FormHint {
        objectName: "MicrophoneReason"
        text: form.microphone.reason || ""
        visible: !!text
    }

    EffectPowerForm {
        Layout.fillWidth: true
        options: form.options

        onEdited: (key, value) => form.edited(key, value)
    }
}
