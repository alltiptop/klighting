// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Item {
    id: root

    readonly property color ambientColor: Kirigami.Theme.linkColor
    required property var controller
    property int focusedId: -1
    required property var keyboard
    readonly property real lineWidth: positioned ? strip.pathWidth * keyboard.scaleFactor : 8
    readonly property color offColor: keyboard.offColor
    readonly property var points: {
        if (positioned)
            return strip.path.map(p => ({
                id: p.id,
                x: keyboard.contentPadding + (p.x - controller.device.x) * keyboard.scaleFactor,
                y: keyboard.contentPadding + (p.y - controller.device.y) * keyboard.scaleFactor
            }));
            const count = strip.elements.length;
            const span = Math.max(1, width - 24);
            let result = [];
            if (count) {
            result.push({
            id: strip.elements[0].id,
            x: 12,
            y: height / 2
    });
            for (let i = 0; i < count; ++i)
            result.push({
            id: strip.elements[i].id,
            x: 12 + (i + 0.5) * span / count,
            y: height / 2
    });
            result.push({
            id: strip.elements[count - 1].id,
            x: 12 + span,
            y: height / 2
    });
    }
            return result;
    }
            property bool positioned: false
            property int rangeAnchor: -1
            readonly property color selectionColor: "white"
            required property var strip

            // Each element owns the nearest half of its adjacent color transitions.
            function anchorFor(id) {
            let best = -1, point = Qt.point(0, 0);
            for (let i = 1; i < points.length; ++i) {
            const a = points[i - 1], b = points[i];
            if (a.id !== id && b.id !== id)
            continue;
            const both = a.id === id && b.id === id;
            const length = Math.hypot(b.x - a.x, b.y - a.y) * (both ? 1 : 0.5);
            if (length > best) {
            best = length;
            point = interpolate(a, b, both ? 0.5 : a.id === id ? 0.25 : 0.75);
    }
    }
            return point;
    }

            function colorFor(id) {
            const style = keyboard.preview.find(p => p.id === id);
            return keyboard.lightingColor(style);
    }

            function elementAt(x, y) {
            let nearest = Math.max(7, lineWidth / 2 + 4), id = -1;
            for (let i = 1; i < points.length; ++i) {
            const a = points[i - 1], b = points[i];
            const dx = b.x - a.x, dy = b.y - a.y;
            const t = Math.max(0, Math.min(1, ((x - a.x) * dx + (y - a.y) * dy) / (dx * dx + dy * dy)));
            const p = interpolate(a, b, t);
            const distance = Math.hypot(x - p.x, y - p.y);
            if (distance < nearest) {
            nearest = distance;
            id = t < 0.5 ? a.id : b.id;
    }
    }
            return id;
    }

            function interpolate(a, b, t) {
            return Qt.point(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
    }

            function selectElement(id, modifiers) {
            if (!controller.device.perStrip) {
            keyboard.toggle(strip.id, modifiers & Qt.ControlModifier);
            return;
    }
            const index = strip.elements.findIndex(e => e.id === id);
            buttons.itemAt(index)?.forceActiveFocus();
            const previous = keyboard.selection;
            if ((modifiers & Qt.ShiftModifier) && previous.indexOf(rangeAnchor) >= 0) {
            const anchor = strip.elements.findIndex(e => e.id === rangeAnchor);
            if (anchor >= 0) {
            keyboard.selection = strip.elements.slice(Math.min(anchor, index), Math.max(anchor, index)
            + 1).map(e => e.id);
            return;
    }
    }
            rangeAnchor = id;
            keyboard.toggle(id, modifiers & Qt.ControlModifier);
    }

            function selected(id) {
            return keyboard.selection.indexOf(id) >= 0 || keyboard.selection.indexOf(strip.id) >= 0 || focusedId
            === id;
    }

            implicitHeight: Kirigami.Units.gridUnit * 2

            onAmbientColorChanged: canvas.requestPaint()
            onFocusedIdChanged: canvas.requestPaint()
            onLineWidthChanged: canvas.requestPaint()
            onOffColorChanged: canvas.requestPaint()
            onPointsChanged: canvas.requestPaint()
            onSelectionColorChanged: canvas.requestPaint()

            Connections {
            function onPreviewChanged() {
            canvas.requestPaint();
    }

            function onSelectionChanged() {
            canvas.requestPaint();
    }

            target: root.keyboard
    }

            Canvas {
            id: canvas

            anchors.fill: parent

            onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            ctx.lineCap = "round";
            function stroke(a, b, width, color) {
            ctx.beginPath();
            ctx.moveTo(a.x, a.y);
            ctx.lineTo(b.x, b.y);
            ctx.lineWidth = width;
            ctx.strokeStyle = color;
            ctx.stroke();
    }
            for (let i = 1; i < root.points.length; ++i) {
            const a = root.points[i - 1], b = root.points[i];
            const first = root.selected(a.id), second = root.selected(b.id);
            if (first || second)
            stroke(root.interpolate(a, b, first ? 0 : 0.5), root.interpolate(a, b, second ? 1 : 0.5),
            root.lineWidth + 6, root.selectionColor);
    }
            for (let i = 1; i < root.points.length; ++i) {
            const a = root.points[i - 1], b = root.points[i];
            const gradient = ctx.createLinearGradient(a.x, a.y, b.x, b.y);
            gradient.addColorStop(0, root.colorFor(a.id));
            gradient.addColorStop(1, root.colorFor(b.id));
            stroke(a, b, root.lineWidth, gradient);
    }
            for (let i = 1; i < root.points.length; ++i) {
            const a = root.points[i - 1], b = root.points[i];
            const first = root.selected(a.id), second = root.selected(b.id);
            if (first || second)
            stroke(root.interpolate(a, b, first ? 0 : 0.5), root.interpolate(a, b, second ? 1 : 0.5),
            root.lineWidth, "#24ffffff");
    }
    }
    }

            Repeater {
            id: buttons

            model: root.strip.elements

            QQC2.AbstractButton {
            id: elementButton

            readonly property point anchor: root.anchorFor(modelData.id)
            required property int index
            required property var modelData

            Accessible.checkable: enabled
            Accessible.checked: root.keyboard.selection.indexOf(modelData.id) >= 0 || root.keyboard.selection.indexOf(
            root.strip.id) >= 0
            Accessible.name: qsTranslate("AmbientLed.StripView", "%1 element %2").arg(root.strip.name).arg(
            modelData.label)
            enabled: root.controller.editableLightbar && root.controller.device.perStrip
            focusPolicy: Qt.StrongFocus
            height: 18
            objectName: "StripElement" + index
            width: Math.max(18, number.implicitWidth + 4)
            x: anchor.x - width / 2
            y: anchor.y - height / 2

            background: Item {
    }
            contentItem: QQC2.Label {
            id: number

            color: "white"
            font.bold: true
            font.pixelSize: 11
            horizontalAlignment: Text.AlignHCenter
            objectName: "StripElementLabel" + elementButton.index
            style: Text.Outline
            styleColor: "#111111"
            text: elementButton.modelData.label
            verticalAlignment: Text.AlignVCenter
    }

            onClicked: {
            root.rangeAnchor = modelData.id;
            root.keyboard.toggle(modelData.id, true);
    }
            onVisualFocusChanged: {
            if (visualFocus)
            root.focusedId = modelData.id;
            else if (root.focusedId === modelData.id)
            root.focusedId = -1;
    }

            MouseArea {
            anchors.fill: parent

            onPressed: event => root.selectElement(elementButton.modelData.id, event.modifiers)
    }
    }
    }

            MouseArea {
            readonly property int hoveredId: containsMouse ? root.elementAt(mouseX, mouseY) : -1

            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            QQC2.ToolTip.text: qsTranslate("AmbientLed.StripView", "%1 element %2").arg(root.strip.name).arg(
            root.strip.elements.find(e => e.id === hoveredId)?.label || "")
            QQC2.ToolTip.visible: hoveredId >= 0
            anchors.fill: parent
            cursorShape: hoveredId >= 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
            enabled: root.controller.editableLightbar
            hoverEnabled: true

            onPressed: event => {
            const id = root.elementAt(event.x, event.y);
            if (id < 0) {
            event.accepted = false;
            return;
    }
            root.selectElement(id, event.modifiers);
    }
    }
    }
