pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qournal

// One item of a toolbar or of the floating toolbox, chosen by its id: a button for an action of the main
// window, or one of the controls defined here (colours, sizes, fonts, ...)
Loader {
    id: loader

    required property string itemId
    /// The main window: its actions and the lists of names
    required property var app
    required property PageCanvas canvas
    required property ColorPalette colorPalette
    required property Theme theme
    property int iconSize: 24
    /// The width the colours may take before they continue on another line
    property real maxWidth: Infinity
    /// The item is in a toolbar at a side of the window: its items are below each other
    property bool vertical: false

    /// The item did something: a floating toolbox closes then
    signal triggered()

    readonly property var action: app.actions[itemId]
    /// A text a plugin shows in the toolbar
    readonly property var placeholder: app.placeholders[itemId]
    readonly property var controls: ({
        "separator": separatorComponent,
        "spacer": spacerComponent,
        "eraserType": eraserTypeComponent,
        "colors": colorsComponent,
        "size": sizeComponent,
        "lineStyle": lineStyleComponent,
        "font": fontComponent,
        "zoomLabel": zoomLabelComponent,
        "zoomSlider": zoomSliderComponent,
        "layer": layerComponent,
        "page": pageComponent,
        "colorSelect": colorSelectComponent
    })

    /// A single colour of the palette, "color:3": its position there, -1 for other items
    readonly property int colorIndex: itemId.startsWith("color:") ? Number(itemId.substring(6)) : -1

    Layout.alignment: vertical ? Qt.AlignHCenter : Qt.AlignVCenter
    Layout.fillWidth: itemId === "spacer" && !vertical
    Layout.fillHeight: itemId === "spacer" && vertical
    /// The toolbar has no room for the item: it is in the menu at the end of the toolbar instead
    property bool overflowed: false
    // Some controls only show for the tools they belong to
    readonly property bool shown: item !== null && item.shown

    visible: shown && !overflowed
    sourceComponent: controls[itemId] ?? (action ? (action.menu || action.options ? splitButtonComponent
                                                                                  : buttonComponent)
                                                 : colorIndex >= 0 ? colorComponent
                                                                   : placeholder ? placeholderComponent : null)

    /// A round button for a colour of the palette
    component Swatch: Rectangle {
        id: swatch

        required property var entry  ///< name and color
        readonly property bool selected: loader.app.colorTool && !loader.app.selectTool
                                         && Qt.colorEqual(loader.canvas.color, entry.color)

        width: 22
        height: 22
        radius: 11
        color: entry.color
        opacity: loader.app.colorTool ? 1 : 0.4
        border.width: selected ? 3 : 1
        border.color: selected ? palette.highlight : palette.mid

        HoverHandler { id: swatchHover }
        TapHandler {
            enabled: loader.app.colorTool
            onTapped: {
                loader.canvas.color = swatch.entry.color
                loader.triggered()
            }
        }

        ToolTip.visible: swatchHover.hovered && entry.name !== ""
        ToolTip.text: entry.name
    }

    Component {
        id: colorComponent

        Swatch {
            // A palette may have fewer colours than the toolbar asks for
            readonly property bool shown: loader.colorIndex < loader.colorPalette.colors.length

            entry: loader.colorPalette.colors[loader.colorIndex] ?? ({ name: "", color: "transparent" })
        }
    }

    // A button with a menu next to it, as the combined buttons of Xournal++. For several tools (the drawing types,
    // the selection tools) it shows the one that is in use and the menu chooses another; for a tool (the pen, the
    // eraser) the menu has its options
    Component {
        id: splitButtonComponent

        Row {
            id: split

            readonly property bool shown: true
            /// What the menu offers: actions with text, icon, checked() and trigger()
            readonly property var entries: (loader.action.menu ?? loader.action.options)
                                           .map(id => loader.app.actions[id])
            /// What the button does: the tool of the menu that is in use (or the first one), or the tool itself
            readonly property var main: loader.action.menu ? (entries.find(entry => entry.checked()) ?? entries[0])
                                                           : loader.action
            readonly property string iconSource: loader.theme.icons[main.icon] ?? ""

            ToolButton {
                id: mainButton

                text: split.main.shortText ?? split.main.text
                icon.source: split.iconSource
                icon.color: "transparent"
                icon.width: loader.iconSize
                icon.height: loader.iconSize
                display: split.iconSource !== "" ? AbstractButton.IconOnly : AbstractButton.TextOnly
                checked: split.main.checked ? split.main.checked() : false
                onClicked: {
                    split.main.trigger()
                    loader.triggered()
                }

                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: split.main.text
            }
            ToolButton {
                implicitWidth: Math.round(loader.iconSize * 0.6)
                implicitHeight: mainButton.implicitHeight
                leftPadding: 0
                rightPadding: 0
                onClicked: entryMenu.popup(mainButton, 0, mainButton.height)

                Accessible.name: loader.action.text
                ToolTip.visible: hovered && !entryMenu.visible
                ToolTip.delay: 600
                ToolTip.text: loader.action.text

                // A small triangle: fonts of some platforms lack the sign for it
                contentItem: Canvas {
                    readonly property color color: loader.theme.colors.buttonText

                    onColorChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        const w = Math.min(width - 2, 9)
                        ctx.reset()
                        ctx.fillStyle = color
                        ctx.beginPath()
                        ctx.moveTo((width - w) / 2, height / 2 - w / 4)
                        ctx.lineTo((width + w) / 2, height / 2 - w / 4)
                        ctx.lineTo(width / 2, height / 2 + w / 4)
                        ctx.closePath()
                        ctx.fill()
                    }
                }

                Menu {
                    id: entryMenu

                    // Not below the status bar or the navigation bar of a phone
                    topMargin: loader.app.safeMargin("top")
                    bottomMargin: loader.app.safeMargin("bottom")
                    leftMargin: loader.app.safeMargin("left")
                    rightMargin: loader.app.safeMargin("right")
                    onAboutToShow: loader.app.fitMenuWidth(entryMenu)

                    Repeater {
                        model: split.entries

                        MenuItem {
                            required property var modelData

                            text: modelData.text
                            icon.source: loader.theme.icons[modelData.icon] ?? ""
                            icon.color: "transparent"
                            checkable: true
                            checked: modelData.checked()
                            onTriggered: {
                                modelData.trigger()
                                // An option of a tool chooses the tool as well
                                if (loader.action.options && !loader.action.checked()) {
                                    loader.action.trigger()
                                }
                                loader.triggered()
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: buttonComponent

        ToolButton {
            readonly property bool shown: true
            // An icon of the theme, or the icon of a plugin
            readonly property string iconSource: loader.action.iconSource !== undefined
                                                 ? (loader.theme.iconsSupported ? loader.action.iconSource : "")
                                                 : (loader.theme.icons[loader.action.icon] ?? "")

            // Without an icon the name of the action is shown, or a short form of it
            text: loader.action.shortText ?? loader.action.text
            icon.source: iconSource
            icon.color: "transparent"  // the icons have their own colours
            icon.width: loader.iconSize
            icon.height: loader.iconSize
            display: iconSource !== "" ? AbstractButton.IconOnly : AbstractButton.TextOnly
            checked: loader.action.checked ? loader.action.checked() : false
            enabled: loader.action.enabled ? loader.action.enabled() : true
            opacity: enabled ? 1 : 0.4  // the icons keep their colours when the button is disabled
            onClicked: {
                loader.action.trigger()
                loader.triggered()
            }

            ToolTip.visible: hovered && text !== loader.action.text || hovered && iconSource !== ""
            ToolTip.delay: 600
            ToolTip.text: loader.action.text
        }
    }

    Component {
        id: placeholderComponent

        Label {
            readonly property bool shown: true

            text: loader.placeholder.value !== "" ? loader.placeholder.value : loader.placeholder.description

            ToolTip.visible: placeholderHover.hovered
            ToolTip.text: loader.placeholder.description

            HoverHandler { id: placeholderHover }
        }
    }

    Component {
        id: separatorComponent

        ToolSeparator {
            readonly property bool shown: true

            orientation: loader.vertical ? Qt.Horizontal : Qt.Vertical
        }
    }

    Component {
        id: spacerComponent

        Item {
            readonly property bool shown: true

            implicitWidth: 8
            implicitHeight: 8
        }
    }

    Component {
        id: eraserTypeComponent

        FitComboBox {
            readonly property bool shown: loader.canvas.tool === PageCanvas.Eraser

            implicitWidth: 130
            model: loader.app.eraserTypes
            currentIndex: loader.canvas.eraserType
            onActivated: index => {
                loader.canvas.eraserType = index
                loader.triggered()
            }
        }
    }

    Component {
        id: colorsComponent

        Flow {
            readonly property bool shown: true

            width: Math.min(loader.colorPalette.colors.length * 30 - 4, loader.maxWidth)
            spacing: 4

            Repeater {
                model: loader.colorPalette.colors

                Swatch {
                    required property var modelData

                    entry: modelData
                }
            }
        }
    }

    Component {
        id: sizeComponent

        FitComboBox {
            readonly property bool shown: true

            implicitWidth: Math.max(110, loader.iconSize * 5)  // room for the longest name in larger styles
            enabled: loader.app.sizeTool
            model: [qsTr("Very fine"), qsTr("Fine"), qsTr("Medium"), qsTr("Thick"), qsTr("Very thick")]
            currentIndex: loader.canvas.toolSize
            onActivated: index => {
                loader.canvas.toolSize = index
                loader.triggered()
            }

            ToolTip.visible: hovered
            ToolTip.text: qsTr("Size of the tool: %1 pt").arg(loader.canvas.thickness)
        }
    }

    Component {
        id: lineStyleComponent

        FitComboBox {
            readonly property bool shown: loader.canvas.tool === PageCanvas.Pen || loader.canvas.hasSelection
            readonly property var styles: loader.app.lineStyles

            implicitWidth: 110
            model: styles.map(style => style.name)
            currentIndex: Math.max(styles.findIndex(style => style.style === loader.canvas.lineStyle), 0)
            onActivated: index => {
                loader.canvas.lineStyle = styles[index].style
                loader.triggered()
            }
        }
    }

    // Font of new texts, of the text that is being edited and of selected texts: a button that shows it, as in
    // Xournal++, and opens the controls for it
    Component {
        id: fontComponent

        ToolButton {
            id: fontButton

            readonly property bool shown: true

            text: loader.canvas.textFamily + " " + Number(loader.canvas.textSize.toFixed(1))
            font.bold: loader.canvas.textBold
            font.italic: loader.canvas.textItalic
            onClicked: fontPopup.open()

            ToolTip.visible: hovered && !fontPopup.visible
            ToolTip.delay: 600
            ToolTip.text: qsTr("Font")

            Popup {
                id: fontPopup

                // Not below the status bar or the navigation bar of a phone
                topMargin: loader.app.safeMargin("top")
                bottomMargin: loader.app.safeMargin("bottom")
                leftMargin: loader.app.safeMargin("left")
                rightMargin: loader.app.safeMargin("right")

                y: loader.vertical ? 0 : fontButton.height
                x: loader.vertical ? fontButton.width : 0
                padding: 6

                Row {
                    spacing: 4

                    FitComboBox {
                        width: 170
                        model: Qt.fontFamilies()
                        currentIndex: model.indexOf(loader.canvas.textFamily)
                        displayText: loader.canvas.textFamily
                        onActivated: index => loader.canvas.textFamily = model[index]
                    }
                    FitComboBox {
                        width: 80
                        editable: true
                        model: loader.app.textSizes
                        editText: Number(loader.canvas.textSize.toFixed(2)).toString()
                        validator: DoubleValidator { bottom: 1; top: 500; decimals: 2 }
                        onActivated: index => loader.canvas.textSize = loader.app.textSizes[index]
                        onAccepted: loader.canvas.textSize = Number.fromLocaleString(Qt.locale(), editText)
                    }
                    ToolButton {
                        text: qsTr("B")
                        font.bold: true
                        checked: loader.canvas.textBold
                        onClicked: loader.canvas.textBold = !loader.canvas.textBold

                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Bold")
                    }
                    ToolButton {
                        text: qsTr("I")
                        font.italic: true
                        checked: loader.canvas.textItalic
                        onClicked: loader.canvas.textItalic = !loader.canvas.textItalic

                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Italic")
                    }
                    FitComboBox {
                        width: 100
                        model: [qsTr("Left"), qsTr("Centre"), qsTr("Right")]
                        currentIndex: Math.max(["left", "center", "right"].indexOf(loader.canvas.textAlign), 0)
                        onActivated: index => loader.canvas.textAlign = ["left", "center", "right"][index]

                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Alignment of the lines")
                    }
                    ToolButton {
                        text: "="
                        checked: loader.canvas.textJustify
                        onClicked: loader.canvas.textJustify = !loader.canvas.textJustify

                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Justified: wrapped lines fill the width")
                    }
                }
            }
        }
    }

    Component {
        id: zoomLabelComponent

        Label {
            readonly property bool shown: true

            width: 48
            horizontalAlignment: Text.AlignHCenter
            text: Math.round(loader.canvas.zoom * 100) + " %"
        }
    }

    Component {
        id: zoomSliderComponent

        Slider {
            readonly property bool shown: true

            implicitWidth: loader.vertical ? implicitHandleWidth + 8 : 120
            implicitHeight: loader.vertical ? 120 : implicitHandleHeight + 8
            orientation: loader.vertical ? Qt.Vertical : Qt.Horizontal
            // The steps are those of a factor: the same way to the left and to the right of 100 %
            from: Math.log(0.3)
            to: Math.log(5)
            value: Math.log(loader.canvas.zoom)
            onMoved: loader.canvas.zoomTo(Math.exp(value))

            ToolTip.visible: hovered || pressed
            ToolTip.text: Math.round(loader.canvas.zoom * 100) + " %"
        }
    }

    // The layer that is drawn on, with a menu as in Xournal++: all layers shown or hidden, new layers, and the
    // layers of the page to choose from
    Component {
        id: layerComponent

        ToolButton {
            id: layerButton

            readonly property bool shown: true
            readonly property var names: loader.canvas.layers.map((layer, index) => layer.name !== ""
                                                                     ? layer.name : qsTr("Layer %1").arg(index + 1))

            text: names[loader.canvas.currentLayer] ?? ""
            icon.source: loader.theme.icons["combo-layer"] ?? ""
            icon.color: "transparent"
            icon.width: loader.iconSize
            icon.height: loader.iconSize
            onClicked: layerMenu.popup(layerButton, loader.vertical ? layerButton.width : 0,
                                       loader.vertical ? 0 : layerButton.height)

            ToolTip.visible: hovered && !layerMenu.visible
            ToolTip.delay: 600
            ToolTip.text: qsTr("The layer that is drawn on")

            Menu {
                id: layerMenu

                onAboutToShow: loader.app.fitMenuWidth(layerMenu)
                // Not below the status bar or the navigation bar of a phone
                topMargin: loader.app.safeMargin("top")
                bottomMargin: loader.app.safeMargin("bottom")
                leftMargin: loader.app.safeMargin("left")
                rightMargin: loader.app.safeMargin("right")

                MenuItem {
                    text: loader.app.actions.layerShowAll.text
                    onTriggered: loader.app.actions.layerShowAll.trigger()
                }
                MenuItem {
                    text: loader.app.actions.layerHideAll.text
                    onTriggered: loader.app.actions.layerHideAll.trigger()
                }
                MenuSeparator {}
                MenuItem {
                    text: loader.app.actions.newLayerAbove.text
                    onTriggered: loader.app.actions.newLayerAbove.trigger()
                }
                MenuItem {
                    text: loader.app.actions.newLayerBelow.text
                    onTriggered: loader.app.actions.newLayerBelow.trigger()
                }
                MenuSeparator {}

                // The top layer first, as in the sidebar. Placed by index: a Repeater in a menu puts its items
                // among the ones before it
                Instantiator {
                    model: layerButton.names.length
                    onObjectAdded: (index, object) => layerMenu.insertItem(6 + index, object)
                    onObjectRemoved: (index, object) => layerMenu.removeItem(object)

                    MenuItem {
                        required property int index
                        readonly property int layerIndex: layerButton.names.length - 1 - index

                        text: layerButton.names[layerIndex] ?? ""
                        checkable: true
                        checked: loader.canvas.currentLayer === layerIndex
                        onTriggered: {
                            loader.canvas.currentLayer = layerIndex
                            loader.triggered()
                        }
                    }
                }
            }
        }
    }

    Component {
        id: pageComponent

        Grid {
            readonly property bool shown: true

            columns: loader.vertical ? 1 : 3
            spacing: 4
            horizontalItemAlignment: Grid.AlignHCenter
            verticalItemAlignment: Grid.AlignVCenter

            Label { text: qsTr("Page") }
            SpinBox {
                from: 1
                to: Math.max(loader.canvas.pageCount, 1)
                editable: true
                value: loader.canvas.currentPage + 1
                onValueModified: loader.canvas.currentPage = value - 1
            }
            Label { text: qsTr("of %1").arg(loader.canvas.pageCount) }
        }
    }

    // The colour of the tool; a click chooses another one
    Component {
        id: colorSelectComponent

        ToolButton {
            readonly property bool shown: true
            readonly property var selectAction: loader.app.actions.colorSelect

            enabled: selectAction.enabled()
            opacity: enabled ? 1 : 0.4
            onClicked: selectAction.trigger()

            contentItem: Item {
                implicitWidth: loader.iconSize
                implicitHeight: loader.iconSize

                Rectangle {
                    anchors.centerIn: parent
                    width: loader.iconSize * 0.8
                    height: width
                    radius: 3
                    color: loader.canvas.color
                    border.width: 1
                    border.color: palette.mid
                }
            }

            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: selectAction.text
        }
    }
}
