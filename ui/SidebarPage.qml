// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import org.kde.kcmutils as KCMUtils
import org.kde.kirigami as Kirigami

KCMUtils.ScrollViewKCM {
    id: page

    readonly property bool allColumns: !Kirigami.ColumnView.view || Kirigami.ColumnView.view.width
                                       >= Kirigami.Units.gridUnit * (navigator.nativeSection ? 42 : 60)
    required property var controller
    property int navigationLevel: 0
    required property var navigator
    required property var recorder

    function open(file, extra = {}) {
        recorder.cancel();
        while (navigator.depth > navigationLevel + 1)
            navigator.pop();
        navigator.push(file, Object.assign({
                                               controller,
                                               navigator,
                                               recorder
                                           }, extra));
    }

    Kirigami.ColumnView.pinned: allColumns || (!navigator.nativeSection && navigationLevel === 0
                                               && Kirigami.ColumnView.view && Kirigami.ColumnView.view.width
                                               >= Kirigami.Units.gridUnit * 36)
    Kirigami.ColumnView.preferredWidth: Kirigami.Units.gridUnit * 12
    implicitHeight: Kirigami.Units.gridUnit * 34
    sidebarMode: true

    // Unpinned child lists scroll away naturally, retaining the host's Back button.
    Kirigami.ColumnView.onPinnedChanged: {
        Qt.callLater(() => {
            // Re-anchor the native row after changing which columns are pinned.
            const view = page.Kirigami.ColumnView.view;
            if (!view)
                return;
            const index = view.currentIndex;
            if (index > navigationLevel) {
                view.currentIndex = navigationLevel;
                view.currentIndex = index;
            }
        });
    }

    Kirigami.Separator {
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.top: parent.top
        objectName: "SidebarSeparator"
        parent: page
        visible: page.Kirigami.ColumnView.pinned
        z: 10
    }
}
