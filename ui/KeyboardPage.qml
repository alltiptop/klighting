// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Window
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: page

    property var cardStates: ({})
    readonly property var controller: currentCard ? currentCard.controller : documentController
    property PresetDeviceCard currentCard: null
    required property var documentController
    readonly property var effectTargets: selectedCards.map(card => ({
        profile: card.profileId,
        layer: card.layerIndex,
        keys: card.selection
    }))
    readonly property real formMaximumWidth: Kirigami.Units.gridUnit * 36
    property var keyGroups: controller.groups()
    readonly property int layerIndex: documentController.selectedLayer
    required property var navigator
    property string presetId: documentController.preset.id
    property var previewWindow: visible && livePreview.checked && !recorder.recording ? Window.window : null
    required property var recorder
    readonly property var selectedCards: {
        let selected = [];
        for (let i = 0; i < devices.count; ++i) {
            const card = devices.itemAt(i) as PresetDeviceCard;
            if (card && (card.selection.length || card.wholeDevice))
                selected.push(card);
        }
        return selected.length ? selected : currentCard ? [currentCard] : [];
    }
    readonly property var selection: currentCard ? currentCard.selection : []
    property var watchedController: null

    function ensureCurrentCard() {
        if (!currentCard && devices.count)
            currentCard = devices.itemAt(0) as PresetDeviceCard;
    }

    function updateKeyTracking() {
        if (watchedController && watchedController !== documentController)
            watchedController.watchLayerKeys(null);
        watchedController = documentController;
        watchedController.watchLayerKeys(previewWindow);
    }

    objectName: "KeyboardPage"
    spacing: Kirigami.Units.largeSpacing

    Component.onDestruction: {
        if (watchedController)
            watchedController.watchLayerKeys(null);
        recorder.cancel();
    }
    onControllerChanged: {
        recorder.cancel();
        keyGroups = controller.groups();
        updateKeyTracking();
    }
    onLayerIndexChanged: recorder.cancel()
    onPresetIdChanged: {
        cardStates = ({});
    }
    onPreviewWindowChanged: updateKeyTracking()

    Connections {
        function onContentChanged() {
            page.keyGroups = page.controller.groups();
        }

        target: page.controller
    }

    QQC2.Label {
        Layout.alignment: Qt.AlignHCenter
        Layout.fillWidth: true
        Layout.maximumWidth: page.formMaximumWidth
        text: qsTranslate("AmbientLed.KeyboardPage",
                          "Only one layer is active: the most complex matching combination. Ties use the last row. Unassigned keys inherit Base.")
        visible: page.documentController.preset.layers.length > 0
        wrapMode: Text.Wrap
    }

    Repeater {
        id: devices

        model: page.documentController.presetDevices

        onItemAdded: (index, item) => {
            const card = item as PresetDeviceCard;
            const saved = page.cardStates[card.profileId];
            if (saved) {
                card.selection = saved.selection;
                card.wholeDevice = saved.wholeDevice;
                card.expanded = !card.collapsible || saved.expanded;
            }
            page.ensureCurrentCard();
        }
        onItemRemoved: (index, item) => {
            const card = item as PresetDeviceCard;
            page.cardStates[card.profileId] = {
                selection: card.selection,
                wholeDevice: card.wholeDevice,
                expanded: card.expanded
            };
            if (page.currentCard === item)
                page.currentCard = null;
            Qt.callLater(page.ensureCurrentCard);
        }

        PresetDeviceCard {
            id: deviceCard

            required property var modelData

            Layout.fillWidth: true
            collapsible: devices.count > 1
            controller: modelData
            documentController: page.documentController
            layerIndex: page.layerIndex
            livePreview: !!page.previewWindow

            onActivated: page.currentCard = deviceCard
        }
    }

    RowLayout {
        Layout.fillWidth: true

        QQC2.CheckBox {
            id: livePreview

            checked: false
            objectName: "LivePreview"
            text: qsTranslate("AmbientLed.LayerEditor", "Live Preview")
        }

        Item {
            Layout.fillWidth: true
        }

        QQC2.Button {
            enabled: page.documentController.addablePresetDevices.length > 0
            icon.name: "list-add"
            objectName: "AddPresetDevice"
            text: qsTranslate("AmbientLed.PresetPage", "Add device…")

            onClicked: deviceMenu.open()

            QQC2.Menu {
                id: deviceMenu

                Instantiator {
                    model: page.documentController.addablePresetDevices

                    delegate: QQC2.MenuItem {
                        required property var modelData

                        text: modelData.name

                        onTriggered: page.documentController.addPresetDevice(modelData.id)
                    }

                    onObjectAdded: (index, object) => deviceMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => deviceMenu.removeItem(object)
                }
            }
        }
    }

    Repeater {
        model: page.currentCard ? page.controller.strips : []

        StripEditor {
            required property var modelData

            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            Layout.maximumWidth: page.formMaximumWidth
            controller: page.controller
            keyboard: page.currentCard
            layerIndex: page.layerIndex
            strip: modelData
        }
    }

    QQC2.Label {
        Layout.alignment: Qt.AlignHCenter
        Layout.fillWidth: true
        Layout.maximumWidth: page.formMaximumWidth
        text: qsTranslate("AmbientLed.KeyboardPage", "Click or drag to select keys; Tab and Space also work.")
        visible: page.controller.editableKeys
        wrapMode: Text.Wrap
    }

    Kirigami.FormLayout {
        Layout.alignment: Qt.AlignHCenter
        Layout.fillWidth: true
        Layout.maximumWidth: page.formMaximumWidth
        enabled: !!page.currentCard && (page.controller.editableKeys || page.controller.editableLightbar)

        RowLayout {
            Kirigami.FormData.label: qsTranslate("AmbientLed.KeyboardPage", "Group:")

            QQC2.ComboBox {
                id: groups

                Accessible.name: qsTranslate("AmbientLed.KeyboardPage", "Key group")
                Layout.fillWidth: true
                model: page.keyGroups
                objectName: "KeyGroupSelector"
                textRole: "name"

                onActivated: page.currentCard.selection = page.keyGroups[currentIndex].ids
            }

            QQC2.Button {
                text: qsTranslate("AmbientLed.KeyboardPage", "Select")

                onClicked: page.currentCard.selection = page.keyGroups[groups.currentIndex].ids
            }

            QQC2.ToolButton {
                Accessible.name: qsTranslate("AmbientLed.KeyboardPage", "Remove group")
                QQC2.ToolTip.text: Accessible.name
                QQC2.ToolTip.visible: hovered
                enabled: page.keyGroups[groups.currentIndex]?.custom ?? false
                icon.name: "edit-delete"

                onClicked: page.controller.removeGroup(page.keyGroups[groups.currentIndex].name)
            }
        }

        QQC2.Label {
            text: assignedEffects.assignmentState.defaults ? qsTranslate("AmbientLed.KeyboardPage",
                                                                         "Default layout") : qsTranslate(
                                                                 "AmbientLed.KeyboardPage",
                                                                 "%1 items selected").arg(
                                                                 page.selectedCards.reduce((count, card)
                                                                                           => count
                                                                                              + card.selection.length,
                                                                                           0))
        }
    }

    DeviceEffectSelection {
        id: multipleEffects

        controller: page.documentController
        targets: page.effectTargets
    }

    AssignedEffects {
        id: assignedEffects

        Layout.alignment: Qt.AlignHCenter
        Layout.fillWidth: true
        Layout.maximumWidth: page.formMaximumWidth
        controller: page.selectedCards.length > 1 ? multipleEffects : page.selectedCards[0]?.controller
                                                    || page.documentController

        layerIndex: page.layerIndex
        selection: page.selectedCards.length === 1 ? page.selectedCards[0].selection : page.effectTargets
        visible: page.selectedCards.length > 0
    }

    Kirigami.FormLayout {
        Layout.alignment: Qt.AlignHCenter
        Layout.fillWidth: true
        Layout.maximumWidth: page.formMaximumWidth
        visible: !!page.currentCard

        RowLayout {
            Kirigami.FormData.label: qsTranslate("AmbientLed.KeyboardPage", "Save selection as:")

            QQC2.TextField {
                id: groupName

                Accessible.name: qsTranslate("AmbientLed.KeyboardPage", "Group name")
                maximumLength: 64
                placeholderText: qsTranslate("AmbientLed.KeyboardPage", "Group name")
            }

            QQC2.Button {
                enabled: page.selection.length > 0 && groupName.text.trim().length > 0
                text: qsTranslate("AmbientLed.KeyboardPage", "Save Group")

                onClicked: page.controller.saveGroup(groupName.text, page.selection)
            }
        }
    }
}
