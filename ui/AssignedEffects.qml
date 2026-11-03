// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root

    property var assignmentState: ({
                                       items: [],
                                       mixed: false,
                                       count: 0
                                   })
    readonly property var choices: controller.effectPresets
    required property var controller
    property var dragOrigin: null
    property int dropIndex: -1
    property bool expanded: false
    property bool expandedAbove: false
    property string expandedEffect: ""
    property int layerIndex: -1
    property int revision: 0
    required property var selection

    function beginDrag(index) {
        dragOrigin = {
            index: index,
            id: effectModel.get(index).id,
            above: effectModel.get(index).above,
            revision: revision,
            layer: layerIndex,
            selection: selection.slice()
        };
    }

    function finishDrag(destination) {
        const origin = dragOrigin;
        if (!origin)
            return;
        destination = Math.max(0, Math.min(effectModel.count - 1, destination));
        while (destination !== origin.index && effectModel.get(destination).above !== origin.above)
            destination += destination > origin.index ? -1 : 1;
        dropIndex = -1;
        dragOrigin = null;
        // Finish the handle's release handler before a controller update changes delegates.
        Qt.callLater(() => {
            if (origin.revision === revision && destination !== origin.index)
                controller.moveAssignedEffect(origin.layer, origin.selection, origin.id, origin.above,
                                              destination - origin.index);
            else
                refresh();
        });
    }

    function refresh() {
        ++revision;
        if (dragOrigin)
            return;
        assignmentState = controller.selectionEffects(layerIndex, selection);
        // Keep delegates and focus alive across moves and property edits.
        for (let i = 0; i < assignmentState.items.length; ++i) {
            const item = assignmentState.items[i];
            let existing = -1;
            for (let j = i; j < effectModel.count; ++j)
                if (effectModel.get(j).id === item.id && effectModel.get(j).above === item.above) {
                    existing = j;
                    break;
                }
            if (existing < 0)
                effectModel.insert(i, item);
            else {
                if (existing !== i)
                    effectModel.move(existing, i, 1);
                effectModel.set(i, item);
            }
        }
        if (effectModel.count > assignmentState.items.length)
            effectModel.remove(assignmentState.items.length, effectModel.count
                               - assignmentState.items.length);
    }

    objectName: "AssignedEffectsEditor"
    spacing: Kirigami.Units.smallSpacing

    Component.onCompleted: refresh()
    onLayerIndexChanged: {
        expanded = false;
        expandedEffect = "";
        refresh();
    }
    onSelectionChanged: {
        expanded = false;
        expandedEffect = "";
        refresh();
    }

    Connections {
        function onContentChanged() {
            root.refresh();
        }

        target: root.controller
    }

    Kirigami.Heading {
        level: 3
        text: root.assignmentState.defaults ? qsTranslate("AmbientLed.AssignedEffects", "Default Effects") :
                                              qsTranslate("AmbientLed.AssignedEffects", "Effects")
    }

    QQC2.Label {
        Layout.fillWidth: true
        text: root.assignmentState.defaults ? qsTranslate("AmbientLed.AssignedEffects",
                                                          "Used by keys and lightbar elements without their own effects.") :
                                              root.assignmentState.mixed ? qsTranslate(
                                                                               "AmbientLed.AssignedEffects",
                                                                               "Different assignments selected. Editing one effect keeps the others.") :
                                                                           qsTranslate(
                                                                               "AmbientLed.AssignedEffects",
                                                                               "Effects blend from top to bottom. Inactive notifications are transparent.")
        wrapMode: Text.Wrap
    }

    ListModel {
        id: effectModel
    }

    ListView {
        id: effectList

        Layout.fillWidth: true
        Layout.preferredHeight: contentHeight
        currentIndex: -1
        interactive: false
        keyNavigationEnabled: false
        model: effectModel
        objectName: "AssignedEffectsList"
        spacing: Kirigami.Units.smallSpacing

        delegate: Loader {
            id: delegateRoot

            required property int index
            required property var model

            height: (item as QQC2.Frame)?.implicitHeight ?? 0
            width: effectList.width
            z: root.dragOrigin && root.dragOrigin.index === index ? 10 : 0

            sourceComponent: Component {
                QQC2.Frame {
                    id: row

                    property Item dropIndicator: Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        color: Kirigami.Theme.highlightColor
                        height: 2
                        parent: row
                        visible: root.dragOrigin !== null && root.dropIndex === row.index && root.dropIndex
                                 !== root.dragOrigin.index
                    }
                    readonly property bool expanded: root.expanded && root.expandedEffect === modelData.id
                                                     && root.expandedAbove === modelData.above
                    readonly property int index: delegateRoot.index
                    readonly property var modelData: delegateRoot.model
                    readonly property var options: {
                        root.revision; // Re-evaluate on pending edits, including local definitions.
                        return root.controller.assignedEffectOptions(modelData.id);
                    }

                    function editId() {
                        if (!modelData.local)
                            return options.id || "";
                        const id = root.controller.localizeEffect(root.layerIndex, root.selection,
                                                                  modelData.id, modelData.above);
                        if (id)
                            root.expandedEffect = id;
                        return id;
                    }

                    objectName: "AssignedEffect:" + modelData.id + (modelData.above ? ":above" : "")
                    width: delegateRoot.width

                    contentItem: ColumnLayout {
                        RowLayout {
                            Layout.fillWidth: true

                            QQC2.ToolButton {
                                id: dragHandle

                                Accessible.name: qsTranslate("AmbientLed.AssignedEffects",
                                                             "Drag to reorder effect")
                                QQC2.ToolTip.text: Accessible.name
                                QQC2.ToolTip.visible: hovered && !dragArea.pressed
                                enabled: !root.assignmentState.mixed && (row.index > 0 && effectModel.get(
                                                                             row.index - 1)?.above
                                                                         === row.modelData.above || row.index
                                                                         + 1 < effectModel.count
                                                                         && effectModel.get(row.index + 1)
                                                                         ?.above === row.modelData.above)
                                icon.name: "application-menu"
                                objectName: "AssignedEffectDragHandle"

                                Keys.onEscapePressed: event => {
                                    ++root.revision;
                                    event.accepted = true;
                                }
                                Keys.onShortcutOverride: event => {
                                    if (event.key === Qt.Key_Escape && root.dragOrigin)
                                        event.accepted = true;
                                }

                                // Keep the row inside its delegate. Reparenting a loaded control while
                                // dragging can retain a removed delegate and leave a visible stale row.
                                MouseArea {
                                    id: dragArea

                                    anchors.fill: parent
                                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                    drag.axis: Drag.YAxis
                                    drag.maximumY: effectList.contentHeight - delegateRoot.y - row.height
                                    drag.minimumY: -delegateRoot.y
                                    drag.target: row
                                    preventStealing: true

                                    drag.onActiveChanged: if (drag.active) {
                                                              root.beginDrag(row.index);
                                                              root.dropIndex = row.index;
                                                              dragHandle.forceActiveFocus();
                                                          }
                                    onCanceled: {
                                        ++root.revision;
                                        row.y = 0;
                                        root.finishDrag(root.dropIndex);
                                    }
                                    onPositionChanged: {
                                        if (!drag.active)
                                            return;
                                        const center = effectList.contentItem.mapFromItem(row, 0, row.height
                                                                                          / 2);
                                        const index = effectList.indexAt(1, center.y);
                                        if (index >= 0)
                                            root.dropIndex = index;
                                    }
                                    onReleased: {
                                        row.y = 0;
                                        root.finishDrag(root.dropIndex);
                                    }
                                }
                            }

                            EffectIcon {
                                color: row.modelData.id === "@color" ? row.modelData.color || "#ffffff" :
                                                                       row.options.solidColor || "#ffffff"
                                objectName: "AssignedEffectIcon"
                                type: row.modelData.type
                            }

                            QQC2.Label {
                                Layout.fillWidth: true
                                QQC2.ToolTip.text: text
                                QQC2.ToolTip.visible: truncated && hover.hovered
                                elide: Text.ElideRight
                                text: row.modelData.name
                                textFormat: Text.PlainText

                                HoverHandler {
                                    id: hover
                                }
                            }

                            QQC2.Label {
                                text: qsTranslate("AmbientLed.AssignedEffects", "%1 / %2").arg(
                                          row.modelData.count).arg(root.assignmentState.count)
                                visible: row.modelData.count < root.assignmentState.count
                            }

                            QQC2.ToolButton {
                                Accessible.name: qsTranslate("AmbientLed.AssignedEffects", "Effect settings")
                                checkable: true
                                checked: row.expanded
                                icon.name: row.expanded ? "arrow-up" : "arrow-down"
                                objectName: "ExpandAssignedEffect"

                                onClicked: {
                                    const open = !row.expanded;
                                    root.expandedEffect = row.modelData.id;
                                    root.expandedAbove = row.modelData.above;
                                    root.expanded = open;
                                }
                            }

                            QQC2.ToolButton {
                                Accessible.name: qsTranslate("AmbientLed.AssignedEffects", "Move effect up")
                                QQC2.ToolTip.text: Accessible.name
                                QQC2.ToolTip.visible: hovered
                                enabled: !root.assignmentState.mixed && row.index > 0 && effectModel.get(
                                             row.index - 1)?.above === row.modelData.above
                                icon.name: "go-up"
                                objectName: "MoveAssignedEffectUp"

                                onClicked: root.controller.moveAssignedEffect(root.layerIndex, root.selection,
                                                                              row.modelData.id,
                                                                              row.modelData.above, -1)
                            }

                            QQC2.ToolButton {
                                Accessible.name: qsTranslate("AmbientLed.AssignedEffects", "Move effect down")
                                QQC2.ToolTip.text: Accessible.name
                                QQC2.ToolTip.visible: hovered
                                enabled: !root.assignmentState.mixed && row.index + 1 < effectModel.count
                                         && effectModel.get(row.index + 1)?.above === row.modelData.above
                                icon.name: "go-down"
                                objectName: "MoveAssignedEffectDown"

                                onClicked: root.controller.moveAssignedEffect(root.layerIndex, root.selection,
                                                                              row.modelData.id,
                                                                              row.modelData.above, 1)
                            }

                            QQC2.ToolButton {
                                Accessible.name: qsTranslate("AmbientLed.AssignedEffects", "Remove effect")
                                QQC2.ToolTip.text: Accessible.name
                                QQC2.ToolTip.visible: hovered
                                icon.name: "edit-delete"
                                objectName: "RemoveAssignedEffect"

                                onClicked: root.controller.removeAssignedEffect(root.layerIndex,
                                                                                root.selection,
                                                                                row.modelData.id,
                                                                                row.modelData.above)
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.largeSpacing
                            visible: row.expanded

                            Kirigami.InlineMessage {
                                Layout.fillWidth: true
                                objectName: "SharedEffectWarning"
                                text: qsTranslate("AmbientLed.AssignedEffects",
                                                  "Shared preset: changing these settings affects every use of this effect.")
                                type: Kirigami.MessageType.Warning
                                visible: !row.modelData.local && !!row.options.id
                            }

                            Kirigami.FormLayout {
                                visible: row.modelData.type === "solid"

                                HexColor {
                                    Kirigami.FormData.label: qsTranslate("AmbientLed.AssignedEffects",
                                                                         "Solid color")
                                    label: qsTranslate("AmbientLed.AssignedEffects", "Solid color")
                                    text: row.modelData.id === "@color" ? row.modelData.color || "" :
                                                                          row.options.solidColor || "#ffffff"

                                    onEdited: color => {
                                        if (row.modelData.id === "@color")
                                            root.controller.setAssignedColor(root.layerIndex, root.selection,
                                                                             color);
                                        else {
                                            const id = row.editId();
                                            if (id)
                                                root.controller.setSolidColor(color, id);
                                        }
                                    }
                                }
                            }

                            Loader {
                                Layout.fillWidth: true
                                active: row.expanded && !!row.options.id && row.modelData.type !== "solid"
                                sourceComponent: row.modelData.type === "brightness" ? brightnessForm :
                                                                                       row.modelData.type
                                                                                       === "notifications"
                                                                                       ? notificationsForm :
                                                                                         ambientForm

                                Component {
                                    id: brightnessForm

                                    BrightnessForm {
                                        options: row.options

                                        onEdited: value => {
                                            const id = row.editId();
                                            if (id)
                                                root.controller.setBrightness(value, id);
                                        }
                                    }
                                }

                                Component {
                                    id: ambientForm

                                    AmbientForm {
                                        controller: root.controller
                                        options: row.options

                                        onEdited: (key, value) => {
                                            const id = row.editId();
                                            if (id)
                                                root.controller.setAmbient(key, value, id);
                                        }
                                    }
                                }

                                Component {
                                    id: notificationsForm

                                    NotificationsForm {
                                        controller: root.controller
                                        options: row.options

                                        onEdited: (key, value) => {
                                            const id = row.editId();
                                            if (id)
                                                root.controller.setNotification(key, value, id);
                                        }
                                    }
                                }
                            }

                            Kirigami.Separator {
                                Layout.fillWidth: true
                            }

                            Kirigami.FormLayout {
                                RowLayout {
                                    Kirigami.FormData.label: qsTranslate("AmbientLed.AssignedEffects",
                                                                         "Intensity:")

                                    QQC2.SpinBox {
                                        Accessible.name: qsTranslate("AmbientLed.AssignedEffects",
                                                                     "Effect intensity in percent")
                                        editable: true
                                        from: 0
                                        objectName: "AssignedEffectOpacity"
                                        to: 100
                                        value: Math.max(0, row.modelData.opacity)

                                        onValueModified: root.controller.updateAssignedEffect(root.layerIndex,
                                                                                              root.selection,
                                                                                              row.modelData.id,
                                                                                              row.modelData.above,
                                                                                              row.modelData.above,
                                                                                              value)
                                    }

                                    QQC2.Label {
                                        text: row.modelData.opacity < 0 ? qsTranslate(
                                                                              "AmbientLed.AssignedEffects",
                                                                              "% (mixed)") : "%"
                                    }
                                }

                                QQC2.CheckBox {
                                    checked: row.modelData.above
                                    objectName: "AssignedEffectAboveLayers"
                                    text: qsTranslate("AmbientLed.AssignedEffects", "Show above all layers")
                                    visible: row.modelData.type === "notifications"

                                    onToggled: {
                                        root.expandedAbove = checked;
                                        root.controller.updateAssignedEffect(root.layerIndex, root.selection,
                                                                             row.modelData.id,
                                                                             row.modelData.above, checked,
                                                                             row.modelData.opacity);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        moveDisplaced: Transition {
            NumberAnimation {
                duration: Kirigami.Units.shortDuration
                easing.type: Easing.InOutQuad
                property: "y"
            }
        }
    }

    RowLayout {
        QQC2.Button {
            enabled: root.controller.device.color
            icon.name: "list-add"
            objectName: "AddAssignedEffect"
            text: qsTranslate("AmbientLed.AssignedEffects", "Add Effect")

            onClicked: addMenu.popup()

            QQC2.Menu {
                id: addMenu

                QQC2.Menu {
                    id: savedMenu

                    enabled: root.choices.length > 0
                    title: qsTranslate("AmbientLed.AssignedEffects", "Saved Presets")

                    Instantiator {
                        model: root.choices

                        onObjectAdded: (index, object) => savedMenu.insertItem(index, object)
                        onObjectRemoved: (index, object) => savedMenu.removeItem(object)

                        QQC2.MenuItem {
                            id: savedEffect

                            required property var modelData

                            objectName: "AddAssignedEffect:" + modelData.id
                            text: modelData.name

                            contentItem: RowLayout {
                                spacing: savedEffect.spacing

                                EffectIcon {
                                    color: savedEffect.modelData.solidColor || "#ffffff"
                                    type: savedEffect.modelData.type
                                }

                                QQC2.Label {
                                    Layout.fillWidth: true
                                    color: savedEffect.highlighted ? savedEffect.palette.highlightedText :
                                                                     savedEffect.palette.text
                                    elide: Text.ElideRight
                                    text: savedEffect.text
                                    textFormat: Text.PlainText
                                }
                            }

                            onTriggered: root.controller.addAssignedEffect(root.layerIndex, root.selection,
                                                                           modelData.id)
                        }
                    }
                }

                QQC2.MenuSeparator {
                }

                Instantiator {
                    model: [
                        {
                            type: "brightness",
                            name: qsTranslate("AmbientLed.AssignedEffects", "New Brightness")
                        },
                        {
                            type: "solid",
                            name: qsTranslate("AmbientLed.AssignedEffects", "New Solid Color")
                        },
                        {
                            type: "ambient",
                            name: qsTranslate("AmbientLed.AssignedEffects", "New Ambient")
                        },
                        {
                            type: "notifications",
                            name: qsTranslate("AmbientLed.AssignedEffects", "New Notifications")
                        }
                    ]

                    onObjectAdded: (index, object) => addMenu.insertItem(index + 2, object)
                    onObjectRemoved: (index, object) => addMenu.removeItem(object)

                    QQC2.MenuItem {
                        required property var modelData

                        objectName: "AddLocalEffect:" + modelData.type
                        text: modelData.name

                        onTriggered: {
                            root.expandedEffect = root.controller.addLocalEffect(root.layerIndex,
                                                                                 root.selection,
                                                                                 modelData.type);
                            root.expandedAbove = false;
                            root.expanded = !!root.expandedEffect;
                        }
                    }
                }
            }
        }

        QQC2.Button {
            enabled: root.controller.device.color
            objectName: "ClearKeyLighting"
            text: qsTranslate("AmbientLed.AssignedEffects", "Turn Off")

            onClicked: root.controller.clearKeyLighting(root.layerIndex, root.selection, false)
        }

        QQC2.Button {
            enabled: root.controller.device.color
            objectName: "InheritKeyLighting"
            text: qsTranslate("AmbientLed.AssignedEffects", "Inherit")
            visible: root.layerIndex >= 0

            onClicked: root.controller.clearKeyLighting(root.layerIndex, root.selection, true)
        }
    }

    QQC2.Label {
        Layout.fillWidth: true
        text: qsTranslate("AmbientLed.AssignedEffects",
                          "These notifications override every layer, including unlit keys.")
        visible: root.assignmentState.items.some(e => e.above)
        wrapMode: Text.Wrap
    }
}
