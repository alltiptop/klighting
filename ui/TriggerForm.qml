// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root

    readonly property var choices: [
        {
            id: "",
            name: qsTranslate("AmbientLed.TriggerForm", "Always"),
            icon: "view-visible"
        },
        {
            id: "microphone",
            name: qsTranslate("AmbientLed.TriggerForm", "Microphone"),
            icon: "audio-input-microphone"
        },
        {
            id: "volume",
            name: qsTranslate("AmbientLed.TriggerForm", "Volume"),
            icon: "audio-volume-high"
        },
        {
            id: "muted",
            name: qsTranslate("AmbientLed.TriggerForm", "Sound muted"),
            icon: "audio-volume-muted"
        },
        {
            id: "playback",
            name: qsTranslate("AmbientLed.TriggerForm", "Audio playing"),
            icon: "media-playback-start"
        },
        {
            id: "power-profile",
            name: qsTranslate("AmbientLed.TriggerForm", "Power profile"),
            icon: "power-profile-balanced"
        },
        {
            id: "display",
            name: qsTranslate("AmbientLed.TriggerForm", "Second display connected"),
            icon: "video-display"
        },
        {
            id: "bluetooth",
            name: qsTranslate("AmbientLed.TriggerForm", "Bluetooth enabled"),
            icon: "preferences-system-bluetooth"
        },
        {
            id: "network",
            name: qsTranslate("AmbientLed.TriggerForm", "Network state"),
            icon: "network-wired"
        },
        {
            id: "touchpad",
            name: qsTranslate("AmbientLed.TriggerForm", "Touchpad enabled"),
            icon: "input-touchpad"
        },
        {
            id: "airplane",
            name: qsTranslate("AmbientLed.TriggerForm", "Airplane mode"),
            icon: "airplane-mode"
        },
        {
            id: "keyboard-brightness",
            name: qsTranslate("AmbientLed.TriggerForm", "Keyboard brightness"),
            icon: "input-keyboard"
        },
        {
            id: "notification",
            name: qsTranslate("AmbientLed.TriggerForm", "Notification"),
            icon: "notifications"
        },
        {
            id: "power-connected",
            name: qsTranslate("AmbientLed.TriggerForm", "Power connected"),
            icon: "ac-adapter"
        },
        {
            id: "dbus",
            name: "D-Bus",
            icon: "system-run"
        }
    ]
    required property var controller
    readonly property bool event: kind === "notification" || kind === "power-connected" || (kind === "dbus"
                                                                                            && options.signal)
    readonly property string kind: options.type || ""
    readonly property bool numeric: kind === "volume" || kind === "keyboard-brightness"
    required property var options

    signal edited(string key, var value)

    Kirigami.FormLayout {
        Layout.fillWidth: true

        RowLayout {
            Kirigami.FormData.label: qsTranslate("AmbientLed.TriggerForm", "Trigger:")

            Kirigami.Icon {
                implicitHeight: implicitWidth
                implicitWidth: Kirigami.Units.iconSizes.small
                source: root.choices[trigger.currentIndex]?.icon || "view-visible"
            }

            QQC2.ComboBox {
                id: trigger

                currentIndex: root.choices.findIndex(entry => entry.id === root.kind)
                model: root.choices
                objectName: "EffectTrigger"
                textRole: "name"
                valueRole: "id"

                delegate: QQC2.ItemDelegate {
                    required property int index
                    required property var modelData

                    highlighted: trigger.highlightedIndex === index
                    icon.name: modelData.icon
                    text: modelData.name
                    width: trigger.width
                }

                onActivated: root.edited("type", currentValue)
            }
        }

        QQC2.ComboBox {
            Kirigami.FormData.label: qsTranslate("AmbientLed.TriggerForm", "State:")
            currentIndex: root.options.value ? 1 : 0
            model: root.kind === "microphone" ? [qsTranslate("AmbientLed.TriggerForm", "Unmuted"), qsTranslate(
                                                     "AmbientLed.TriggerForm", "Muted")] : [qsTranslate(
                                                                                                "AmbientLed.TriggerForm",
                                                                                                "No"), qsTranslate(
                                                                                                "AmbientLed.TriggerForm",
                                                                                                "Yes")]
            objectName: "TriggerBoolean"
            visible: !!root.kind && !root.event && !root.numeric && !["dbus", "network", "power-profile"].includes(
                root.kind)

            onActivated: root.edited("value", currentIndex === 1)
        }

        QQC2.ComboBox {
            Kirigami.FormData.label: qsTranslate("AmbientLed.TriggerForm", "Power profile:")
            currentIndex: ["performance", "balanced", "power-saver"].indexOf(root.options.value)
            model: [qsTranslate("AmbientLed.TriggerForm", "Performance"), qsTranslate("AmbientLed.TriggerForm",
                                                                                      "Balanced"), qsTranslate(
                    "AmbientLed.TriggerForm", "Power saver")]
            visible: root.kind === "power-profile"

            onActivated: root.edited("value", ["performance", "balanced", "power-saver"][currentIndex])
        }

        QQC2.ComboBox {
            Kirigami.FormData.label: qsTranslate("AmbientLed.TriggerForm", "Network state:")
            currentIndex: [10, 20, 30, 40, 50, 60, 70].indexOf(root.options.value)
            model: [qsTranslate("AmbientLed.TriggerForm", "Asleep"), qsTranslate("AmbientLed.TriggerForm",
                                                                                 "Disconnected"), qsTranslate(
                    "AmbientLed.TriggerForm", "Disconnecting"), qsTranslate("AmbientLed.TriggerForm",
                                                                            "Connecting"), qsTranslate(
                    "AmbientLed.TriggerForm", "Local connection"), qsTranslate("AmbientLed.TriggerForm",
                                                                               "Site connection"), qsTranslate(
                    "AmbientLed.TriggerForm", "Internet connection")]
            visible: root.kind === "network"

            onActivated: root.edited("value", [10, 20, 30, 40, 50, 60, 70][currentIndex])
        }

        QQC2.ComboBox {
            Kirigami.FormData.label: qsTranslate("AmbientLed.TriggerForm", "Condition:")
            currentIndex: ["eq", "ne", "gt", "ge", "lt", "le", "any"].indexOf(root.options.comparison)
            model: ["=", "≠", ">", "≥", "<", "≤", qsTranslate("AmbientLed.TriggerForm", "Any value")]
            objectName: "TriggerComparison"
            visible: root.numeric || root.kind === "dbus"

            onActivated: root.edited("comparison", ["eq", "ne", "gt", "ge", "lt", "le", "any"][currentIndex])
        }

        QQC2.SpinBox {
            Kirigami.FormData.label: qsTranslate("AmbientLed.TriggerForm", "Threshold (%):")
            editable: true
            from: 0
            objectName: "TriggerThreshold"
            to: root.kind === "volume" ? 150 : 100
            value: Number(root.options.value) || 0
            visible: root.numeric

            onValueModified: root.edited("value", value)
        }

        QQC2.SpinBox {
            Kirigami.FormData.label: qsTranslate("AmbientLed.TriggerForm", "Event duration (ms):")
            editable: true
            from: 100
            objectName: "TriggerDuration"
            stepSize: 100
            to: 60000
            value: root.options.duration || 3000
            visible: root.event

            onValueModified: root.edited("duration", value)
        }
    }

    Loader {
        Layout.fillWidth: true
        active: !!root.controller && root.kind === "microphone"
        visible: active

        sourceComponent: MicrophoneForm {
            controller: root.controller
            options: root.options
            triggerOnly: true
        }
    }

    NotificationTriggerForm {
        Layout.fillWidth: true
        options: root.options
        visible: root.kind === "notification"

        onEdited: (key, value) => root.edited(key, value)
    }

    Kirigami.FormLayout {
        Layout.fillWidth: true
        visible: root.kind === "dbus"

        QQC2.ComboBox {
            Kirigami.FormData.label: qsTranslate("AmbientLed.TriggerForm", "Bus:")
            currentIndex: root.options.bus === "system" ? 1 : 0
            model: [qsTranslate("AmbientLed.TriggerForm", "Session"), qsTranslate("AmbientLed.TriggerForm",
                                                                                  "System")]

            onActivated: root.edited("bus", currentIndex ? "system" : "session")
        }

        QQC2.ComboBox {
            Kirigami.FormData.label: qsTranslate("AmbientLed.TriggerForm", "Observe:")
            currentIndex: root.options.signal ? 1 : 0
            model: [qsTranslate("AmbientLed.TriggerForm", "Property"), qsTranslate("AmbientLed.TriggerForm",
                                                                                   "Signal")]

            onActivated: root.edited("signal", currentIndex === 1)
        }

        Repeater {
            model: [
                {
                    key: "service",
                    label: qsTranslate("AmbientLed.TriggerForm", "Service:")
                },
                {
                    key: "path",
                    label: qsTranslate("AmbientLed.TriggerForm", "Object path:")
                },
                {
                    key: "interface",
                    label: qsTranslate("AmbientLed.TriggerForm", "Interface:")
                },
                {
                    key: "member",
                    label: qsTranslate("AmbientLed.TriggerForm", "Property or signal:")
                }
            ]

            QQC2.TextField {
                required property var modelData

                Kirigami.FormData.label: modelData.label
                Layout.fillWidth: true
                maximumLength: 256
                text: root.options[modelData.key] || ""

                onEditingFinished: root.edited(modelData.key, text)
            }
        }

        QQC2.ComboBox {
            Kirigami.FormData.label: qsTranslate("AmbientLed.TriggerForm", "Value type:")
            currentIndex: typeof root.options.value === "boolean" ? 0 : typeof root.options.value === "number"
                                                                    ? 1 : 2
            model: [qsTranslate("AmbientLed.TriggerForm", "Boolean"), qsTranslate("AmbientLed.TriggerForm",
                                                                                  "Number"), qsTranslate(
                    "AmbientLed.TriggerForm", "Text")]

            onActivated: root.edited("value", [true, 0, ""][currentIndex])
        }

        QQC2.TextField {
            Kirigami.FormData.label: qsTranslate("AmbientLed.TriggerForm", "Value:")
            Layout.fillWidth: true
            enabled: root.options.comparison !== "any"
            maximumLength: 256
            text: String(root.options.value ?? "")

            onEditingFinished: {
                if (typeof root.options.value === "boolean") {
                    if (text === "true" || text === "false")
                        root.edited("value", text === "true");
                } else if (typeof root.options.value === "number") {
                    if (text.trim() && Number.isFinite(Number(text)))
                        root.edited("value", Number(text));
                } else
                    root.edited("value", text);
            }
        }

        FormHint {
            text: qsTranslate("AmbientLed.TriggerForm",
                              "Signals compare their first argument. A signal without arguments has the value true. Only observation is supported.")
        }
    }

    FormHint {
        Layout.fillWidth: true
        text: qsTranslate("AmbientLed.TriggerForm",
                          "Unavailable sources leave the effect inactive. Repeated events are coalesced until the current event ends.")
        visible: !!root.kind
    }
}
