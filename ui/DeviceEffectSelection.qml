// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

QtObject {
    id: root

    required property var controller
    readonly property var device: {
        revision;
        return controller.deviceSelectionCapabilities(targets);
    }
    property Connections documentChanges: Connections {
        function onContentChanged() {
            ++root.revision;
            root.contentChanged();
        }

        target: root.controller
    }
    readonly property var effect: controller.effect
    readonly property var effectPresets: controller.effectPresets
    property int revision: 0
    required property var targets

    signal contentChanged

    function addAssignedEffect(layer, keys, id, above = false) {
        apply("add", {
                  id: id,
                  above: above
              });
    }

    function addLocalEffect(layer, keys, type) {
        return apply("new", {
                         type: type
                     });
    }

    function apply(action, args) {
        return controller.editDeviceSelection(targets, action, args);
    }

    function assignedEffectOptions(id) {
        return controller.deviceSelectionEffectOptions(targets, id);
    }

    function clearKeyLighting(layer, keys, inherit) {
        apply("clear", {
                  inherit: inherit
              });
    }

    function localizeEffect(layer, keys, id, above) {
        return apply("localize", {
                         id: id,
                         above: above
                     });
    }

    function moveAssignedEffect(layer, keys, id, above, offset) {
        apply("move", {
                  id: id,
                  above: above,
                  offset: offset
              });
    }

    function removeAssignedEffect(layer, keys, id, above) {
        apply("remove", {
                  id: id,
                  above: above
              });
    }

    function selectionEffects(layer, keys) {
        return controller.deviceSelectionEffects(targets);
    }

    function setAmbient(key, value, id) {
        apply("ambient", {
                  id: id,
                  key: key,
                  value: value
              });
    }

    function setAssignedColor(layer, keys, value) {
        apply("color", {
                  value: value
              });
    }

    function setBrightness(value, id) {
        apply("brightness", {
                  id: id,
                  value: value
              });
    }

    function setNotification(key, value, id) {
        apply("notifications", {
                  id: id,
                  key: key,
                  value: value
              });
    }

    function setSolidColor(value, id) {
        apply("solid", {
                  id: id,
                  value: value
              });
    }

    function updateAssignedEffect(layer, keys, id, wasAbove, above, opacity) {
        apply("update", {
                  id: id,
                  wasAbove: wasAbove,
                  above: above,
                  opacity: opacity
              });
    }

    onTargetsChanged: contentChanged()
}
