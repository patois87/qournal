pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qournal

// One toolbar of the window: items of the toolbar configuration in a row, or in a column at a side of the window.
// What does not fit goes into a menu behind a button at the end, as in the toolbars of Xournal++ (GTK)
ToolBar {
    id: strip

    /// The ids of the items, see ToolbarItem
    required property var items
    required property var app
    required property PageCanvas canvas
    required property ColorPalette colorPalette
    required property Theme theme
    property int iconSize: 24
    property bool vertical: false
    /// Shown before the items
    property Component leading: null

    /// The ids of the items that are in the menu
    property var overflowIds: []
    /// The entries of the menu, see overflowEntries()
    readonly property var overflowMenuEntries: overflowEntries(overflowIds)

    /// Hides the items from the first one that does not fit, keeping room for the button of the menu
    function relayout() {
        const available = (vertical ? height : width) - 16
                          - (leadingLoader.item ? (vertical ? leadingLoader.height : leadingLoader.width) + 4 : 0)
        const button = (vertical ? overflowButton.implicitHeight : overflowButton.implicitWidth) + 4
        const sizes = []
        let total = 0
        for (let i = 0; i < repeater.count; ++i) {
            const item = repeater.itemAt(i)
            const size = item && item.shown ? (vertical ? item.implicitHeight : item.implicitWidth) + 4 : 0
            sizes.push(size)
            total += size
        }
        let cut = sizes.length
        if (total > available) {
            let used = 0
            cut = 0
            while (cut < sizes.length && used + sizes[cut] <= available - button) {
                used += sizes[cut]
                ++cut
            }
        }
        const ids = []
        for (let i = 0; i < repeater.count; ++i) {
            const item = repeater.itemAt(i)
            if (item) {
                item.overflowed = i >= cut
                if (i >= cut && item.shown) {
                    ids.push(items[i])
                }
            }
        }
        if (JSON.stringify(ids) !== JSON.stringify(overflowIds)) {
            overflowIds = ids
        }
    }

    function scheduleRelayout() {
        relayoutTimer.restart()
    }

    function actionEntry(action, indent) {
        const source = action.iconSource !== undefined ? (theme.iconsSupported ? action.iconSource : "")
                                                       : (theme.icons[action.icon] ?? "")
        return {
            kind: "action", text: action.text, icon: source, indent: indent,
            checkable: action.checked !== undefined,
            checked: action.checked ?? (() => false),
            enabled: action.enabled ?? (() => true),
            trigger: action.trigger
        }
    }

    function colorEntry(color) {
        return {
            kind: "color", text: color.name !== "" ? color.name : color.color.toString(), color: color.color,
            indent: false, checkable: true,
            checked: () => app.colorTool && Qt.colorEqual(canvas.color, color.color),
            enabled: () => app.colorTool,
            trigger: () => canvas.color = color.color
        }
    }

    /// What the menu shows for the items that do not fit: buttons and colours as entries, a group of buttons
    /// (sizes, line styles) as its entries, other controls as an entry that shows the control
    function overflowEntries(ids) {
        const entries = []
        const separator = () => {
            if (entries.length > 0 && entries[entries.length - 1].kind !== "separator") {
                entries.push({ kind: "separator" })
            }
        }
        const groups = {
            "size": ["sizeVeryFine", "sizeFine", "sizeMedium", "sizeThick", "sizeVeryThick"],
            "lineStyle": ["linePlain", "lineDashed", "lineDashDotted", "lineDotted"],
            "eraserType": ["eraserStandard", "eraserWhiteout", "eraserDeleteStroke"]
        }
        for (const id of ids) {
            const action = app.actions[id]
            if (id === "separator") {
                separator()
            } else if (id === "spacer") {
                continue
            } else if (id.startsWith("color:")) {
                const color = colorPalette.colors[Number(id.substring(6))]
                if (color) {
                    entries.push(colorEntry(color))
                }
            } else if (id === "colors") {
                separator()
                for (const color of colorPalette.colors) {
                    entries.push(colorEntry(color))
                }
                separator()
            } else if (groups[id]) {
                separator()
                for (const member of groups[id]) {
                    entries.push(actionEntry(app.actions[member], false))
                }
                separator()
            } else if (action && (action.menu || action.options)) {
                // The tool, then what its menu has
                separator()
                if (action.options) {
                    entries.push(actionEntry(action, false))
                }
                for (const member of action.menu ?? action.options) {
                    entries.push(actionEntry(app.actions[member], action.options !== undefined))
                }
                separator()
            } else if (action && id !== "colorSelect") {
                entries.push(actionEntry(action, false))
            } else {
                entries.push({
                    kind: "control", id: id, text: app.itemName(id), icon: "", indent: false, checkable: false,
                    checked: () => false, enabled: () => true, trigger: () => controlPopup.show(id)
                })
            }
        }
        while (entries.length > 0 && entries[entries.length - 1].kind === "separator") {
            entries.pop()
        }
        return entries
    }

    /// The edge of the window this toolbar is at: "top", "bottom", "left", "right", or "" if another one is
    /// between it and the edge
    property string edge: ""
    // What Qt adds to the padding of a toolbar at an edge of the screen (the status bar, a camera in the screen,
    // rounded corners; since Qt 6.9): the toolbar is that much larger, or its items would be pushed out of it.
    // Taken from the window and not from the toolbar itself, whose own margins depend on where it is, which
    // depends on its size
    readonly property real safeTop: edge === "top" ? app.safeMargin("top") : 0
    readonly property real safeBottom: edge === "bottom" ? app.safeMargin("bottom") : 0
    readonly property real safeLeft: edge === "left" ? app.safeMargin("left") : 0
    readonly property real safeRight: edge === "right" ? app.safeMargin("right") : 0

    implicitHeight: vertical ? 0 : Math.max(48, iconSize + 24) + safeTop + safeBottom
    implicitWidth: vertical ? Math.max(48, layout.implicitWidth + 12) + safeLeft + safeRight : 0
    // The colours of the theme in every style: the icons are drawn for them
    background: Rectangle { color: strip.theme.colors.window }

    onWidthChanged: scheduleRelayout()
    onHeightChanged: scheduleRelayout()
    onItemsChanged: scheduleRelayout()
    // The menu and the control from it belong to the items that did not fit
    onVisibleChanged: {
        if (!visible) {
            overflowMenu.close()
            controlPopup.close()
        }
    }
    onOverflowIdsChanged: {
        if (overflowIds.length === 0) {
            overflowMenu.close()
            controlPopup.close()
        }
    }

    Timer {
        id: relayoutTimer

        interval: 0
        onTriggered: strip.relayout()
    }

    GridLayout {
        id: layout

        x: strip.vertical ? (strip.availableWidth - width) / 2 : 8
        y: strip.vertical ? 8 : 0
        width: strip.vertical ? implicitWidth : strip.availableWidth - 16
        height: strip.vertical ? strip.availableHeight - 16 : strip.availableHeight
        flow: strip.vertical ? GridLayout.TopToBottom : GridLayout.LeftToRight
        rows: strip.vertical ? -1 : 1
        columns: strip.vertical ? 1 : -1
        rowSpacing: 4
        columnSpacing: 4

        Loader {
            id: leadingLoader

            Layout.alignment: strip.vertical ? Qt.AlignHCenter : Qt.AlignVCenter
            active: strip.leading !== null
            visible: active
            sourceComponent: strip.leading
        }

        Repeater {
            id: repeater

            model: strip.items

            ToolbarItem {
                required property string modelData

                itemId: modelData
                app: strip.app
                canvas: strip.canvas
                colorPalette: strip.colorPalette
                theme: strip.theme
                iconSize: strip.iconSize
                vertical: strip.vertical
                // In a column the colours are below each other
                maxWidth: strip.vertical ? 30 : Infinity
                onShownChanged: strip.scheduleRelayout()
                onImplicitWidthChanged: strip.scheduleRelayout()
                onImplicitHeightChanged: strip.scheduleRelayout()
            }
        }

        // Takes the room that is left, so that the items stay together; a spacer among them takes it instead
        Item {
            Layout.fillWidth: !strip.vertical
            Layout.fillHeight: strip.vertical
            visible: !strip.items.includes("spacer") || strip.overflowIds.length > 0
        }

        ToolButton {
            id: overflowButton

            objectName: "toolbarOverflow"

            Layout.alignment: strip.vertical ? Qt.AlignHCenter : Qt.AlignVCenter
            visible: strip.overflowIds.length > 0
            text: "»"
            rotation: strip.vertical ? 90 : 0
            font.pixelSize: Math.round(strip.iconSize * 0.8)
            onClicked: overflowMenu.popup(overflowButton, strip.vertical ? overflowButton.width : 0,
                                          strip.vertical ? 0 : overflowButton.height)

            Accessible.name: qsTr("More")
            ToolTip.visible: hovered && !overflowMenu.visible
            ToolTip.delay: 600
            ToolTip.text: qsTr("What does not fit into the toolbar")
        }
    }

    Menu {
        id: overflowMenu

        onAboutToShow: strip.app.fitMenuWidth(overflowMenu)
        // Not below the status bar or the navigation bar of a phone
        topMargin: strip.app.safeMargin("top")
        bottomMargin: strip.app.safeMargin("bottom")
        leftMargin: strip.app.safeMargin("left")
        rightMargin: strip.app.safeMargin("right")

        Repeater {
            model: strip.overflowMenuEntries

            MenuItem {
                id: entryItem

                required property var modelData
                readonly property bool isSeparator: modelData.kind === "separator"
                // Shown by the frame around the icon: the check box of the style would cover the icon
                readonly property bool active: !isSeparator && modelData.checked()

                height: isSeparator ? 9 : implicitHeight
                enabled: !isSeparator && modelData.enabled()
                text: isSeparator ? "" : modelData.text
                onTriggered: {
                    if (!isSeparator) {
                        modelData.trigger()
                    }
                }

                contentItem: Item {
                    implicitWidth: row.implicitWidth
                    implicitHeight: entryItem.isSeparator ? 1 : row.implicitHeight

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: 1
                        visible: entryItem.isSeparator
                        color: palette.mid
                    }
                    Row {
                        id: row

                        visible: !entryItem.isSeparator
                        x: entryItem.modelData.indent ? 16 : 0
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8

                        Item {
                            width: 24
                            height: 24

                            Rectangle {
                                anchors.fill: parent
                                radius: 4
                                visible: entryItem.active
                                color: palette.highlight
                                opacity: 0.35
                            }
                            Image {
                                anchors.centerIn: parent
                                width: 20
                                height: 20
                                visible: entryItem.modelData.kind === "action"
                                source: entryItem.modelData.icon ?? ""
                                sourceSize: Qt.size(20, 20)
                            }
                            Rectangle {
                                anchors.centerIn: parent
                                width: 16
                                height: 16
                                radius: 8
                                visible: entryItem.modelData.kind === "color"
                                color: entryItem.modelData.color ?? "transparent"
                                border.width: 1
                                border.color: palette.mid
                            }
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: entryItem.text
                            font.bold: entryItem.active
                            opacity: entryItem.enabled ? 1 : 0.5
                        }
                    }
                }
            }
        }
    }

    // A control that does not fit (a slider, the page number, ...), shown below or next to the button of the menu
    Popup {
        id: controlPopup

        // Not below the status bar or the navigation bar of a phone
        topMargin: strip.app.safeMargin("top")
        bottomMargin: strip.app.safeMargin("bottom")
        leftMargin: strip.app.safeMargin("left")
        rightMargin: strip.app.safeMargin("right")

        property string itemId: ""

        function show(id) {
            itemId = id
            const pos = overflowButton.mapToItem(strip, 0, 0)
            x = strip.vertical ? pos.x + overflowButton.width : Math.max(0, pos.x + overflowButton.width - width)
            y = strip.vertical ? pos.y : pos.y + overflowButton.height
            open()
        }

        padding: 6

        ToolbarItem {
            itemId: controlPopup.itemId
            app: strip.app
            canvas: strip.canvas
            colorPalette: strip.colorPalette
            theme: strip.theme
            iconSize: strip.iconSize
        }
    }
}
