pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Qournal

// The settings of the application. Changes apply at once and are kept for the next sessions
Dialog {
    id: dialog

    /// The main window: its names of the tools and its settings of the user interface
    required property var app
    required property PageCanvas canvas
    required property Theme theme
    required property Localization localization
    required property AudioController audio
    readonly property InputSettings input: canvas.input
    // Incremented when the buttons change, so that the boxes show the new state
    property int buttonRevision: 0

    /// The pen input has a dialog of its own
    signal penInputRequested()

    readonly property var buttons: [
        { button: PageCanvas.ButtonEraserTip, name: qsTr("Eraser end of the pen") },
        { button: PageCanvas.ButtonStylus1, name: qsTr("First button of the pen") },
        { button: PageCanvas.ButtonStylus2, name: qsTr("Second button of the pen") },
        { button: PageCanvas.ButtonMouseMiddle, name: qsTr("Middle mouse button") },
        { button: PageCanvas.ButtonMouseRight, name: qsTr("Right mouse button") },
        { button: PageCanvas.ButtonMouse4, name: qsTr("Fourth mouse button (back)") },
        { button: PageCanvas.ButtonMouse5, name: qsTr("Fifth mouse button (forward)") }
    ]
    // What a button can do: the selected tool, one of the tools, or the floating toolbox
    readonly property var buttonActions: [PageCanvas.ButtonNoAction].concat(app.toolNames.map((name, tool) => tool),
                                                                            [PageCanvas.ButtonFloatingToolbox])
    readonly property var buttonActionNames: [qsTr("The selected tool")].concat(app.toolNames,
                                                                               [qsTr("Show Floating Toolbox")])
    readonly property var canvasColors: ["#5a5a60", "#2e2e32", "#8a8a90", "#c8c8cc", "#3b4a5a", "#000000"]

    function describeTemplate() {
        const t = canvas.pageTemplate
        if (t.width === undefined) {
            return qsTr("A4, plain")
        }
        const millimetres = points => Math.round(points / 72 * 25.4)
        return qsTr("%1 × %2 mm, %3").arg(millimetres(t.width)).arg(millimetres(t.height)).arg(t.style ?? "plain")
    }

    anchors.centerIn: parent
    /// On a phone: the forms have one column, with each label above its control
    readonly property bool narrow: width < 34 * font.pixelSize
    /// The row of a cell of a form with two columns, which is one column when the dialog is narrow
    function cell(row, column) {
        return narrow ? 2 * row + column : row
    }

    width: Math.min(parent.width - 32, 600 * Math.max(1, font.pixelSize / 13))
    height: narrow ? parent.height - 32 : Math.min(parent.height - 32, 560 * Math.max(1, font.pixelSize / 13))
    modal: true
    Overlay.modal: ModalDim {}
    title: qsTr("Preferences")
    standardButtons: Dialog.Close

    onClosed: canvas.saveSettings()

    Connections {
        target: dialog.canvas

        function onButtonActionsChanged() {
            dialog.buttonRevision++
        }
    }

    component Heading: Label {
        Layout.columnSpan: dialog.narrow ? 1 : 2
        Layout.topMargin: 8
        font.bold: true
    }

    component Note: Label {
        Layout.columnSpan: dialog.narrow ? 1 : 2
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        opacity: 0.7
    }

    /// A check box over the whole width, whose text wraps: the long ones made the page wider than the dialog
    /// where the font is larger (Android), and what was on the right was cut off
    component Option: WrapCheckBox {
        Layout.columnSpan: dialog.narrow ? 1 : 2
    }

    /// A colour that is chosen with a dialog; apply() gets the new one
    component ColorButton: Rectangle {
        id: colorButton

        property var apply: color => {}

        implicitWidth: 26
        implicitHeight: 26
        border.width: 1
        border.color: palette.mid

        TapHandler {
            onTapped: {
                colorDialog.apply = colorButton.apply
                colorDialog.selectedColor = colorButton.color
                colorDialog.open()
            }
        }
    }

    /// A percentage of a fraction between 0 and 1
    component PercentBox: SpinBox {
        property real fraction
        signal fractionModified(real fraction)

        from: 0
        to: 100
        editable: true
        value: Math.round(fraction * 100)
        onValueModified: fractionModified(value / 100)
    }

    ColumnLayout {
        anchors.fill: parent

        TabBar {
            id: tabs

            Layout.fillWidth: true
            visible: !dialog.narrow

            TabButton { text: qsTr("Input") }
            TabButton { text: qsTr("Touch") }
            TabButton { text: qsTr("View") }
            TabButton { text: qsTr("Defaults") }
            TabButton { text: qsTr("Audio") }
        }
        // On a phone the tabs do not fit next to each other: a list to choose the page from instead
        ComboBox {
            Layout.fillWidth: true
            visible: dialog.narrow
            model: [qsTr("Input"), qsTr("Touch"), qsTr("View"), qsTr("Defaults"), qsTr("Audio")]
            currentIndex: tabs.currentIndex
            onActivated: index => tabs.currentIndex = index
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // Input devices and their buttons
            ScrollView {
                id: inputPage

                contentWidth: availableWidth

                ColumnLayout {
                    width: inputPage.availableWidth

                    GridLayout {
                        Layout.fillWidth: true
                        columns: dialog.narrow ? 1 : 2
                        columnSpacing: 12

                        Heading { text: qsTr("Buttons") }
                        Note { text: qsTr("The tool that is used as long as a button is pressed.") }

                        Repeater {
                            model: dialog.buttons

                            Label {
                                required property var modelData
                                required property int index

                                Layout.row: dialog.cell(2 + 2 * index, 0)
                                Layout.column: dialog.narrow ? 0 : 0
                                text: modelData.name
                            }
                        }
                        Repeater {
                            model: dialog.buttons

                            ComboBox {
                                required property var modelData
                                required property int index

                                Layout.row: dialog.cell(2 + 2 * index, 1)
                                Layout.column: dialog.narrow ? 0 : 1
                                Layout.fillWidth: true
                                model: dialog.buttonActionNames
                                currentIndex: (dialog.buttonRevision, dialog.buttonActions.indexOf(
                                                   dialog.canvas.buttonAction(modelData.button)))
                                onActivated: index => dialog.canvas.setButtonAction(modelData.button,
                                                                                    dialog.buttonActions[index])
                            }
                        }
                        // What the pen or highlighter of a button draws, and how: only shown for these tools
                        Repeater {
                            model: dialog.buttons

                            RowLayout {
                                id: optionsRow

                                required property var modelData
                                required property int index
                                readonly property int action: (dialog.buttonRevision,
                                                               dialog.canvas.buttonAction(modelData.button))
                                readonly property var options: (dialog.buttonRevision,
                                                                dialog.canvas.buttonOptions(modelData.button))

                                Layout.row: dialog.cell(3 + 2 * index, 1)
                                Layout.column: dialog.narrow ? 0 : 1
                                Layout.fillWidth: true
                                visible: action === PageCanvas.Pen || action === PageCanvas.Highlighter
                                         || action === PageCanvas.Eraser

                                ComboBox {
                                    Layout.fillWidth: true
                                    visible: optionsRow.action !== PageCanvas.Eraser
                                    model: [qsTr("Drawing type of the tool")].concat(dialog.app.drawingTypes)
                                    currentIndex: optionsRow.options.drawingType + 1
                                    onActivated: index => dialog.canvas.setButtonOptions(optionsRow.modelData.button,
                                                                                         { drawingType: index - 1 })
                                }
                                ComboBox {
                                    Layout.fillWidth: true
                                    model: [qsTr("Size of the tool"), qsTr("Very fine"), qsTr("Fine"), qsTr("Medium"),
                                            qsTr("Thick"), qsTr("Very thick")]
                                    currentIndex: optionsRow.options.size + 1
                                    onActivated: index => dialog.canvas.setButtonOptions(optionsRow.modelData.button,
                                                                                         { size: index - 1 })
                                }
                                Rectangle {
                                    readonly property bool own: optionsRow.options.color.valid === true
                                                                || optionsRow.options.color.a > 0

                                    visible: optionsRow.action !== PageCanvas.Eraser
                                    implicitWidth: 26
                                    implicitHeight: 26
                                    radius: 13
                                    color: own ? optionsRow.options.color : "transparent"
                                    border.width: 1
                                    border.color: palette.mid

                                    Label {
                                        Layout.fillWidth: dialog.narrow
                                        wrapMode: Text.Wrap
                                        anchors.centerIn: parent
                                        visible: !parent.own
                                        text: "–"
                                    }
                                    TapHandler {
                                        onTapped: {
                                            buttonColorDialog.button = optionsRow.modelData.button
                                            buttonColorDialog.open()
                                        }
                                    }
                                    ToolTip.visible: buttonColorHover.hovered
                                    ToolTip.text: qsTr("Colour of the button; none for the colour of the tool")

                                    HoverHandler { id: buttonColorHover }
                                }
                                ToolButton {
                                    visible: optionsRow.action !== PageCanvas.Eraser
                                    text: "×"
                                    onClicked: dialog.canvas.setButtonOptions(optionsRow.modelData.button,
                                                                              { color: "#00000000" })

                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("The colour of the tool")
                                }
                            }
                        }

                        Heading {
                            Layout.row: dialog.cell(2 + 2 * dialog.buttons.length, 0)
                            text: qsTr("Pen")
                        }
                        Option {
                            Layout.row: dialog.cell(3 + 2 * dialog.buttons.length, 0)
                            text: qsTr("The pen draws wider with more pressure")
                            checked: dialog.canvas.usePressure
                            onToggled: dialog.canvas.usePressure = checked
                        }
                        Button {
                            Layout.row: dialog.cell(4 + 2 * dialog.buttons.length, 0)
                            Layout.columnSpan: dialog.narrow ? 1 : 2
                            text: qsTr("Pressure curve and stabilizer…")
                            onClicked: dialog.penInputRequested()
                        }
                        Option {
                            Layout.row: dialog.cell(5 + 2 * dialog.buttons.length, 0)
                            text: qsTr("Guess the pressure from the speed, for devices without pressure")
                            checked: dialog.input.pressureGuessing
                            onToggled: dialog.input.pressureGuessing = checked
                        }
                        Label {
                            Layout.fillWidth: dialog.narrow
                            wrapMode: Text.Wrap
                            Layout.row: dialog.cell(6 + 2 * dialog.buttons.length, 0)
                            Layout.column: dialog.narrow ? 0 : 0
                            text: qsTr("Square of the eraser")
                        }
                        ComboBox {
                            Layout.row: dialog.cell(6 + 2 * dialog.buttons.length, 1)
                            Layout.column: dialog.narrow ? 0 : 1
                            Layout.fillWidth: true
                            model: [qsTr("Never shown"), qsTr("Always shown"), qsTr("While it hovers"),
                                    qsTr("While it erases")]
                            currentIndex: dialog.input.eraserVisibility
                            onActivated: index => dialog.input.eraserVisibility = index
                        }
                        Label {
                            Layout.fillWidth: dialog.narrow
                            wrapMode: Text.Wrap
                            Layout.row: dialog.cell(7 + 2 * dialog.buttons.length, 0)
                            Layout.column: dialog.narrow ? 0 : 0
                            text: qsTr("Pointer of the pen")
                        }
                        ComboBox {
                            Layout.row: dialog.cell(7 + 2 * dialog.buttons.length, 1)
                            Layout.column: dialog.narrow ? 0 : 1
                            Layout.fillWidth: true
                            model: [qsTr("None"), qsTr("Dot"), qsTr("Big dot"), qsTr("Arrow")]
                            currentIndex: dialog.input.stylusCursor
                            onActivated: index => dialog.input.stylusCursor = index
                        }
                        Label {
                            Layout.fillWidth: dialog.narrow
                            wrapMode: Text.Wrap
                            Layout.row: dialog.cell(8 + 2 * dialog.buttons.length, 0)
                            Layout.column: dialog.narrow ? 0 : 0
                            text: qsTr("Events left out at the start of a stroke")
                        }
                        SpinBox {
                            Layout.row: dialog.cell(8 + 2 * dialog.buttons.length, 1)
                            Layout.column: dialog.narrow ? 0 : 1
                            from: 0
                            to: 10
                            editable: true
                            value: dialog.input.ignoredStylusEvents
                            onValueModified: dialog.input.ignoredStylusEvents = value
                        }
                        Heading {
                            Layout.row: dialog.cell(9 + 2 * dialog.buttons.length, 0)
                            text: qsTr("Drawing")
                        }
                        Option {
                            Layout.row: dialog.cell(10 + 2 * dialog.buttons.length, 0)
                            text: qsTr("A tap is not a stroke: it selects what is under it")
                            checked: dialog.input.strokeFilterEnabled
                            onToggled: dialog.input.strokeFilterEnabled = checked
                        }
                        Label {
                            Layout.fillWidth: dialog.narrow
                            wrapMode: Text.Wrap
                            Layout.row: dialog.cell(11 + 2 * dialog.buttons.length, 0)
                            Layout.column: dialog.narrow ? 0 : 0
                            text: qsTr("A tap is shorter than (ms)")
                        }
                        SpinBox {
                            Layout.row: dialog.cell(11 + 2 * dialog.buttons.length, 1)
                            Layout.column: dialog.narrow ? 0 : 1
                            enabled: dialog.input.strokeFilterEnabled
                            from: 10
                            to: 2000
                            stepSize: 10
                            editable: true
                            value: dialog.input.strokeFilterTime
                            onValueModified: dialog.input.strokeFilterTime = value
                        }
                        Label {
                            Layout.fillWidth: dialog.narrow
                            wrapMode: Text.Wrap
                            Layout.row: dialog.cell(12 + 2 * dialog.buttons.length, 0)
                            Layout.column: dialog.narrow ? 0 : 0
                            text: qsTr("Dots right after a stroke are kept for (ms)")
                        }
                        SpinBox {
                            Layout.row: dialog.cell(12 + 2 * dialog.buttons.length, 1)
                            Layout.column: dialog.narrow ? 0 : 1
                            enabled: dialog.input.strokeFilterEnabled
                            from: 0
                            to: 5000
                            stepSize: 50
                            editable: true
                            value: dialog.input.strokeFilterSuccessive
                            onValueModified: dialog.input.strokeFilterSuccessive = value
                        }
                        Option {
                            Layout.row: dialog.cell(13 + 2 * dialog.buttons.length, 0)
                            text: qsTr("Drawing a shape to the left acts like Shift, drawing it upwards like Control")
                            checked: dialog.input.drawDirModsEnabled
                            onToggled: dialog.input.drawDirModsEnabled = checked
                        }
                        Option {
                            Layout.row: dialog.cell(14 + 2 * dialog.buttons.length, 0)
                            text: qsTr("Recognized shapes snap to the grid")
                            checked: dialog.input.snapRecognizedShapes
                            onToggled: dialog.input.snapRecognizedShapes = checked
                        }
                        Label {
                            Layout.fillWidth: dialog.narrow
                            wrapMode: Text.Wrap
                            Layout.row: dialog.cell(15 + 2 * dialog.buttons.length, 0)
                            Layout.column: dialog.narrow ? 0 : 0
                            text: qsTr("Tolerance of snapping to the grid (%)")
                        }
                        PercentBox {
                            Layout.row: dialog.cell(15 + 2 * dialog.buttons.length, 1)
                            Layout.column: dialog.narrow ? 0 : 1
                            fraction: dialog.input.snapGridTolerance
                            onFractionModified: fraction => dialog.input.snapGridTolerance = fraction
                        }
                        Label {
                            Layout.fillWidth: dialog.narrow
                            wrapMode: Text.Wrap
                            Layout.row: dialog.cell(16 + 2 * dialog.buttons.length, 0)
                            Layout.column: dialog.narrow ? 0 : 0
                            text: qsTr("Tolerance of snapping to angles (%)")
                        }
                        PercentBox {
                            Layout.row: dialog.cell(16 + 2 * dialog.buttons.length, 1)
                            Layout.column: dialog.narrow ? 0 : 1
                            fraction: dialog.input.snapRotationTolerance
                            onFractionModified: fraction => dialog.input.snapRotationTolerance = fraction
                        }
                        Label {
                            Layout.fillWidth: dialog.narrow
                            wrapMode: Text.Wrap
                            Layout.row: dialog.cell(20 + 2 * dialog.buttons.length, 0)
                            Layout.column: dialog.narrow ? 0 : 0
                            text: qsTr("The laser pointer starts to fade after (ms)")
                        }
                        SpinBox {
                            Layout.row: dialog.cell(20 + 2 * dialog.buttons.length, 1)
                            Layout.column: dialog.narrow ? 0 : 1
                            from: 0
                            to: 10000
                            stepSize: 100
                            editable: true
                            value: dialog.input.laserFadeOutTime
                            onValueModified: dialog.input.laserFadeOutTime = value
                        }
                        Heading {
                            Layout.row: dialog.cell(17 + 2 * dialog.buttons.length, 0)
                            text: qsTr("Moving a selection to the edge of the view")
                        }
                        Label {
                            Layout.fillWidth: dialog.narrow
                            wrapMode: Text.Wrap
                            Layout.row: dialog.cell(18 + 2 * dialog.buttons.length, 0)
                            Layout.column: dialog.narrow ? 0 : 0
                            text: qsTr("Speed of the view (% of it per second)")
                        }
                        SpinBox {
                            Layout.row: dialog.cell(18 + 2 * dialog.buttons.length, 1)
                            Layout.column: dialog.narrow ? 0 : 1
                            from: 0
                            to: 500
                            editable: true
                            value: Math.round(dialog.input.edgePanSpeed)
                            onValueModified: dialog.input.edgePanSpeed = value
                        }
                        Label {
                            Layout.fillWidth: dialog.narrow
                            wrapMode: Text.Wrap
                            Layout.row: dialog.cell(19 + 2 * dialog.buttons.length, 0)
                            Layout.column: dialog.narrow ? 0 : 0
                            text: qsTr("Faster deep in the edge, up to (times)")
                        }
                        SpinBox {
                            Layout.row: dialog.cell(19 + 2 * dialog.buttons.length, 1)
                            Layout.column: dialog.narrow ? 0 : 1
                            from: 1
                            to: 20
                            editable: true
                            value: Math.round(dialog.input.edgePanMaxMult)
                            onValueModified: dialog.input.edgePanMaxMult = value
                        }
                    }

                    // What each device is used as, as in Xournal++: a tablet that reports itself as a mouse can be
                    // used as a pen, a touchscreen can be switched off
                    Label {
                        Layout.fillWidth: dialog.narrow
                        wrapMode: Text.Wrap
                        Layout.topMargin: 8
                        font.bold: true
                        text: qsTr("Devices")
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        opacity: 0.7
                        text: qsTr("What each input device is used as. Devices that were not used yet may be missing.")
                    }
                    Repeater {
                        id: deviceRepeater

                        model: dialog.visible ? dialog.canvas.inputDevices() : []

                        RowLayout {
                            id: deviceRow

                            required property var modelData

                            Layout.fillWidth: true

                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: deviceRow.modelData.name + " (" + deviceRow.modelData.type + ")"
                            }
                            ComboBox {
                                readonly property var names: [qsTr("Disabled"), qsTr("Mouse"), qsTr("Pen"),
                                                              qsTr("Eraser"), qsTr("Touchscreen")]

                                Layout.preferredWidth: 220
                                // The first entry: what the device is without a choice
                                model: [qsTr("Automatic (%1)").arg(names[deviceRow.modelData.automaticClass])]
                                       .concat(names)
                                currentIndex: deviceRow.modelData.deviceClass + 1
                                onActivated: index => dialog.canvas.setDeviceClass(deviceRow.modelData.name, index - 1)
                            }
                        }
                    }
                }
            }

            // Touch screens
            ScrollView {
                id: touchPage

                contentWidth: availableWidth

                GridLayout {
                    width: touchPage.availableWidth
                    columns: dialog.narrow ? 1 : 2
                    columnSpacing: 12

                    Heading { text: qsTr("Fingers") }
                    Option {
                        text: qsTr("One finger draws with the selected tool instead of moving the view")
                        checked: dialog.canvas.fingerDraws
                        onToggled: dialog.canvas.fingerDraws = checked
                    }
                    Option {
                        text: qsTr("Two fingers zoom")
                        checked: dialog.input.zoomGestures
                        onToggled: dialog.input.zoomGestures = checked
                    }
                    Label {
                        Layout.fillWidth: dialog.narrow
                        wrapMode: Text.Wrap
                        text: qsTr("They zoom once their distance changed by (%)")
                    }
                    SpinBox {
                        enabled: dialog.input.zoomGestures
                        from: 0
                        to: 100
                        editable: true
                        value: Math.round(dialog.input.touchZoomThreshold)
                        onValueModified: dialog.input.touchZoomThreshold = value
                    }

                    Heading { text: qsTr("Palm rejection") }
                    Option {
                        text: qsTr("Ignore touches for a moment after the pen was used")
                        checked: dialog.input.palmRejection
                        onToggled: dialog.input.palmRejection = checked
                    }
                    Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("Milliseconds") }
                    SpinBox {
                        enabled: dialog.input.palmRejection
                        from: 100
                        to: 5000
                        stepSize: 100
                        editable: true
                        value: dialog.input.palmRejectionTime
                        onValueModified: dialog.input.palmRejectionTime = value
                    }
                    Note { text: qsTr("Touches are always ignored while the pen is on the screen.") }
                }
            }

            // Zoom and the look of the application
            ScrollView {
                id: viewPage

                contentWidth: availableWidth

                GridLayout {
                    width: viewPage.availableWidth
                    columns: dialog.narrow ? 1 : 2
                    columnSpacing: 12

                    Heading { text: qsTr("Appearance") }
                    Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("Colours") }
                    ComboBox {
                        readonly property var modes: ["system", "light", "dark", "eink"]

                        Layout.fillWidth: true
                        model: [qsTr("As the system"), qsTr("Light"), qsTr("Dark"), qsTr("Electronic paper")]
                        currentIndex: Math.max(modes.indexOf(dialog.app.themeMode), 0)
                        onActivated: index => dialog.app.themeMode = modes[index]
                    }
                    Note {
                        text: qsTr("Electronic paper: black on white without grays, and no pointer of the pen. A "
                                   + "device with such a screen has it by itself. On Android and iOS the controls "
                                   + "change their look when the application is started the next time.")
                    }
                    Option {
                        enabled: dialog.theme.eink
                        text: qsTr("Smooth edges of strokes on electronic paper (they look fainter there)")
                        checked: dialog.input.einkSmoothing
                        onToggled: dialog.input.einkSmoothing = checked
                    }
                    Option {
                        enabled: dialog.theme.eink
                        text: qsTr("Fills as a pattern of dots on electronic paper (tones look uneven there)")
                        checked: dialog.input.einkPatternFills
                        onToggled: dialog.input.einkPatternFills = checked
                    }
                    Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("Arrangement") }
                    ComboBox {
                        readonly property var modes: ["auto", "desktop", "touch"]

                        Layout.fillWidth: true
                        model: [qsTr("As the device"), qsTr("For mouse and keyboard"), qsTr("For fingers and a pen")]
                        currentIndex: Math.max(modes.indexOf(dialog.app.layoutMode), 0)
                        onActivated: index => dialog.app.layoutMode = modes[index]
                    }
                    Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("Icons") }
                    ComboBox {
                        readonly property var themes: ["lucide", "color", "none"]

                        Layout.fillWidth: true
                        enabled: dialog.theme.iconsSupported
                        model: [qsTr("Lucide"), qsTr("Colourful"), qsTr("Text instead of icons")]
                        currentIndex: dialog.theme.iconsSupported ? Math.max(themes.indexOf(dialog.app.iconTheme), 0)
                                                                  : 2
                        onActivated: index => dialog.app.iconTheme = themes[index]
                    }
                    Note {
                        visible: !dialog.theme.iconsSupported
                        text: qsTr("This build cannot show icons: the SVG plugin of Qt is missing.")
                    }
                    Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("Size of the icons") }
                    SpinBox {
                        from: 16
                        to: 64
                        stepSize: 4
                        value: dialog.app.iconSize
                        onValueModified: dialog.app.iconSize = value
                    }
                    Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("Around the pages") }
                    Row {
                        spacing: 6

                        Repeater {
                            model: dialog.canvasColors

                            Rectangle {
                                id: colorChoice

                                required property string modelData

                                width: 26
                                height: 26
                                color: modelData
                                border.width: Qt.colorEqual(dialog.canvas.canvasColor, modelData) ? 3 : 1
                                border.color: border.width > 1 ? palette.highlight : palette.mid

                                TapHandler { onTapped: dialog.canvas.canvasColor = colorChoice.modelData }
                            }
                        }
                        Button {
                            height: 26
                            text: qsTr("Other…")
                            onClicked: {
                                canvasColorDialog.selectedColor = dialog.canvas.canvasColor
                                canvasColorDialog.open()
                            }
                        }
                    }
                    Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("Language") }
                    ComboBox {
                        // The first entry stands for the language of the system
                        readonly property var codes: [""].concat(dialog.localization.languages.map(l => l.code))

                        Layout.fillWidth: true
                        model: [qsTr("As the system")].concat(dialog.localization.languages.map(l => l.name))
                        currentIndex: Math.max(codes.indexOf(dialog.localization.language), 0)
                        onActivated: index => dialog.localization.language = codes[index]
                    }
                    Note { text: qsTr("The language changes when the application is started the next time.") }

                    Heading { text: qsTr("Zoom") }
                    Label {
                        Layout.fillWidth: dialog.narrow
                        wrapMode: Text.Wrap
                        text: qsTr("Step of zooming in and out (%)")
                    }
                    SpinBox {
                        from: 1
                        to: 100
                        editable: true
                        value: Math.round(dialog.input.zoomStep)
                        onValueModified: dialog.input.zoomStep = value
                    }
                    Label {
                        Layout.fillWidth: dialog.narrow
                        wrapMode: Text.Wrap
                        text: qsTr("Step of Control and the wheel (%)")
                    }
                    SpinBox {
                        from: 1
                        to: 100
                        editable: true
                        value: Math.round(dialog.input.wheelZoomStep)
                        onValueModified: dialog.input.wheelZoomStep = value
                    }
                    Label {
                        Layout.fillWidth: dialog.narrow
                        wrapMode: Text.Wrap
                        text: qsTr("Resolution of the screen (dpi)")
                    }
                    RowLayout {
                        SpinBox {
                            id: dpiBox

                            from: 20
                            to: 1000
                            editable: true
                            value: Math.round(dialog.canvas.displayDpi)
                            onValueModified: dialog.canvas.displayDpi = value
                        }
                        Button {
                            text: qsTr("From the screen")
                            onClicked: dialog.canvas.displayDpi = Screen.logicalPixelDensity * 25.4
                        }
                    }
                    Note {
                        text: qsTr("At a zoom of 100 % the pages have their real size when this is right: the bar "
                                   + "below is 5 cm long then. 72 makes a point of the page a pixel.")
                    }
                    Rectangle {
                        Layout.columnSpan: dialog.narrow ? 1 : 2
                        Layout.preferredWidth: dialog.canvas.displayDpi / 2.54 * 5
                        Layout.preferredHeight: 8
                        color: palette.text
                    }

                    Heading { text: qsTr("Pages") }
                    Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("Scroll bars") }
                    ComboBox {
                        readonly property var places: ["auto", "right", "left", "hidden"]

                        Layout.fillWidth: true
                        model: [qsTr("Right, none in the arrangement for fingers"), qsTr("Right"), qsTr("Left"),
                                qsTr("Hidden")]
                        currentIndex: Math.max(places.indexOf(dialog.app.scrollbars), 0)
                        onActivated: index => dialog.app.scrollbars = places[index]
                    }
                    Option {
                        text: qsTr("The pages can be scrolled beyond their edges")
                        checked: dialog.canvas.unlimitedScrolling
                        onToggled: dialog.canvas.unlimitedScrolling = checked
                    }
                    Label {
                        Layout.fillWidth: dialog.narrow
                        wrapMode: Text.Wrap
                        text: qsTr("Space above and below the pages (pt)")
                    }
                    RowLayout {
                        SpinBox {
                            from: 0
                            to: 5000
                            stepSize: 10
                            editable: true
                            value: Math.round(dialog.canvas.spaceAbove)
                            onValueModified: dialog.canvas.spaceAbove = value
                        }
                        SpinBox {
                            from: 0
                            to: 5000
                            stepSize: 10
                            editable: true
                            value: Math.round(dialog.canvas.spaceBelow)
                            onValueModified: dialog.canvas.spaceBelow = value
                        }
                    }
                    Label {
                        Layout.fillWidth: dialog.narrow
                        wrapMode: Text.Wrap
                        text: qsTr("Space left and right of the pages (pt)")
                    }
                    RowLayout {
                        SpinBox {
                            from: 0
                            to: 5000
                            stepSize: 10
                            editable: true
                            value: Math.round(dialog.canvas.spaceLeft)
                            onValueModified: dialog.canvas.spaceLeft = value
                        }
                        SpinBox {
                            from: 0
                            to: 5000
                            stepSize: 10
                            editable: true
                            value: Math.round(dialog.canvas.spaceRight)
                            onValueModified: dialog.canvas.spaceRight = value
                        }
                    }
                    Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("Colour of selections") }
                    ColorButton {
                        color: dialog.canvas.selectionColor
                        apply: color => dialog.canvas.selectionColor = color
                    }

                    Heading { text: qsTr("Sidebar") }
                    Option {
                        text: qsTr("On the right side")
                        checked: dialog.app.sidebarRight
                        onToggled: dialog.app.sidebarRight = checked
                    }
                    Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("Numbers of the pages") }
                    ComboBox {
                        readonly property var styles: ["below", "circle", "square", "none"]

                        Layout.fillWidth: true
                        model: [qsTr("Below the preview"), qsTr("In a circle on the preview"),
                                qsTr("In a square on the preview"), qsTr("None")]
                        currentIndex: Math.max(styles.indexOf(dialog.app.previewNumbers), 0)
                        onActivated: index => dialog.app.previewNumbers = styles[index]
                    }

                    Heading { text: qsTr("Title of the window") }
                    Option {
                        text: qsTr("The whole path of the file")
                        checked: dialog.app.titleShowsPath
                        onToggled: dialog.app.titleShowsPath = checked
                    }
                    Option {
                        text: qsTr("The number of the current page")
                        checked: dialog.app.titleShowsPage
                        onToggled: dialog.app.titleShowsPage = checked
                    }

                    Heading { text: qsTr("Full screen and presentation") }
                    GridLayout {
                        Layout.columnSpan: dialog.narrow ? 1 : 2
                        columns: 3
                        columnSpacing: 12

                        Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("Shown") }
                        Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("Full screen") }
                        Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("Presentation") }
                        Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("Menu bar") }
                        CheckBox {
                            checked: dialog.app.fullScreenMenubar
                            onToggled: dialog.app.fullScreenMenubar = checked
                        }
                        CheckBox {
                            checked: dialog.app.presentationMenubar
                            onToggled: dialog.app.presentationMenubar = checked
                        }
                        Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("Toolbars") }
                        CheckBox {
                            checked: dialog.app.fullScreenToolbars
                            onToggled: dialog.app.fullScreenToolbars = checked
                        }
                        CheckBox {
                            checked: dialog.app.presentationToolbars
                            onToggled: dialog.app.presentationToolbars = checked
                        }
                        Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("Sidebar") }
                        CheckBox {
                            checked: dialog.app.fullScreenSidebar
                            onToggled: dialog.app.fullScreenSidebar = checked
                        }
                        CheckBox {
                            checked: dialog.app.presentationSidebar
                            onToggled: dialog.app.presentationSidebar = checked
                        }
                    }

                    Heading { text: qsTr("Highlighted position of the pointer") }
                    Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("Colour and border") }
                    RowLayout {
                        ColorButton {
                            color: dialog.canvas.positionColor
                            apply: color => dialog.canvas.positionColor = color
                        }
                        ColorButton {
                            color: dialog.canvas.positionBorderColor
                            apply: color => dialog.canvas.positionBorderColor = color
                        }
                    }
                    Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("Radius (px)") }
                    SpinBox {
                        from: 1
                        to: 500
                        editable: true
                        value: Math.round(dialog.canvas.positionRadius)
                        onValueModified: dialog.canvas.positionRadius = value
                    }
                    Label {
                        Layout.fillWidth: dialog.narrow
                        wrapMode: Text.Wrap
                        text: qsTr("Width of the border (px)")
                    }
                    SpinBox {
                        from: 0
                        to: 50
                        editable: true
                        value: Math.round(dialog.canvas.positionBorderWidth)
                        onValueModified: dialog.canvas.positionBorderWidth = value
                    }
                }
            }

            // What new documents and sessions start with
            ScrollView {
                id: defaultsPage

                contentWidth: availableWidth

                GridLayout {
                    width: defaultsPage.availableWidth
                    columns: dialog.narrow ? 1 : 2
                    columnSpacing: 12

                    Heading { text: qsTr("Page of new documents") }
                    Label {
                        Layout.fillWidth: dialog.narrow
                        wrapMode: Text.Wrap
                        Layout.columnSpan: dialog.narrow ? 1 : 2
                        text: dialog.describeTemplate()
                    }
                    Button {
                        text: qsTr("Use the current page")
                        onClicked: dialog.canvas.pageTemplate = dialog.canvas.pageProperties(dialog.canvas.currentPage)
                    }
                    Button {
                        text: qsTr("A4, plain")
                        onClicked: dialog.canvas.pageTemplate = ({})
                    }

                    Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("Unit of the page size") }
                    ComboBox {
                        readonly property var units: ["cm", "mm", "in", "pt"]

                        Layout.fillWidth: true
                        model: [qsTr("Centimetres"), qsTr("Millimetres"), qsTr("Inches"), qsTr("Points")]
                        currentIndex: Math.max(units.indexOf(dialog.app.paperUnit), 0)
                        onActivated: index => dialog.app.paperUnit = units[index]
                    }
                    Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("A new page is appended") }
                    ComboBox {
                        Layout.fillWidth: true
                        model: [qsTr("Never by itself"), qsTr("When the last page is written on"),
                                qsTr("When the view is scrolled to the end")]
                        currentIndex: dialog.canvas.appendPage
                        onActivated: index => dialog.canvas.appendPage = index
                    }
                    Label {
                        Layout.fillWidth: dialog.narrow
                        wrapMode: Text.Wrap
                        text: qsTr("Spaces of the Tab key in texts")
                    }
                    SpinBox {
                        from: 0
                        to: 16
                        editable: true
                        value: dialog.input.tabSpaces
                        onValueModified: dialog.input.tabSpaces = value
                    }
                    Note { text: qsTr("0 puts a tab character into the text.") }

                    Heading { text: qsTr("Files") }
                    Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("Name of new documents") }
                    TextField {
                        Layout.fillWidth: true
                        text: dialog.app.saveNamePattern
                        onEditingFinished: dialog.app.saveNamePattern = text
                    }
                    Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("Name of exported files") }
                    TextField {
                        Layout.fillWidth: true
                        text: dialog.app.exportNamePattern
                        onEditingFinished: dialog.app.exportNamePattern = text
                    }
                    Note {
                        text: qsTr("%{name} stands for the name of the document or of its PDF, %F for the date, %H and "
                                   + "%M for the hour and minute; the codes of strftime are understood.")
                    }
                    Option {
                        text: qsTr("A PDF file opens the document that annotates it, if there is one next to it")
                        checked: dialog.app.openAnnotationOfPdf
                        onToggled: dialog.app.openAnnotationOfPdf = checked
                    }
                    Option {
                        text: qsTr("Open the last document at the start")
                        checked: dialog.app.openLastAtStart
                        onToggled: dialog.app.openLastAtStart = checked
                    }

                    Heading { text: qsTr("Autosave") }
                    Option {
                        text: qsTr("Save unsaved changes to a separate file from time to time")
                        checked: dialog.canvas.autosaveEnabled
                        onToggled: dialog.canvas.autosaveEnabled = checked
                    }
                    Label {
                        Layout.fillWidth: dialog.narrow
                        wrapMode: Text.Wrap
                        text: qsTr("Minutes between two saves")
                    }
                    SpinBox {
                        enabled: dialog.canvas.autosaveEnabled
                        from: 1
                        to: 60
                        editable: true
                        value: dialog.canvas.autosaveInterval
                        onValueModified: dialog.canvas.autosaveInterval = value
                    }

                    Heading { text: qsTr("LaTeX") }
                    Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("Command") }
                    TextField {
                        Layout.fillWidth: true
                        text: dialog.app.latexCommand
                        onEditingFinished: dialog.app.latexCommand = text
                    }
                    Note { text: qsTr("{} stands for the .tex file. An empty field restores the default.") }
                    Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("Template") }
                    RowLayout {
                        Layout.fillWidth: true

                        TextField {
                            Layout.fillWidth: true
                            placeholderText: qsTr("The template of Xournal++")
                            text: dialog.app.latexTemplate
                            onEditingFinished: dialog.app.latexTemplate = text
                        }
                        Button {
                            text: qsTr("Choose…")
                            onClicked: templateDialog.open()
                        }
                    }

                    Heading { text: qsTr("All settings") }
                    Button {
                        Layout.columnSpan: dialog.narrow ? 1 : 2
                        text: qsTr("Reset to the defaults")
                        onClicked: {
                            dialog.canvas.resetSettings()
                            dialog.app.resetInterfaceSettings()
                        }
                    }
                }
            }

            // Recording and playback
            ScrollView {
                id: audioPage

                contentWidth: availableWidth

                GridLayout {
                    width: audioPage.availableWidth
                    columns: dialog.narrow ? 1 : 2
                    columnSpacing: 12

                    Note {
                        visible: !dialog.audio.available
                        text: qsTr("This build cannot record or play audio: it was built without Qt Multimedia.")
                    }

                    Heading { text: qsTr("Recordings") }
                    Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("Folder") }
                    RowLayout {
                        Layout.fillWidth: true

                        TextField {
                            Layout.fillWidth: true
                            text: dialog.audio.folder
                            onEditingFinished: dialog.audio.folder = text
                        }
                        Button {
                            text: qsTr("Choose…")
                            onClicked: audioFolderDialog.open()
                        }
                    }
                    Note {
                        text: qsTr("The document only notes the names of the recordings. They are looked for in this "
                                   + "folder and next to the document. An empty field restores the default.")
                    }

                    Note {
                        visible: dialog.audio.recordsVorbis
                        text: qsTr("Recordings are Ogg Vorbis files, as those of Xournal++.")
                    }
                    Option {
                        visible: !dialog.audio.recordsVorbis
                        text: qsTr("Prefer small files to files Xournal++ can play")
                        enabled: dialog.audio.available
                        checked: dialog.audio.compact
                        onToggled: dialog.audio.compact = checked
                    }
                    Note {
                        visible: !dialog.audio.recordsVorbis
                        text: qsTr("Xournal++ records Ogg Vorbis. Where Qt cannot write that, the recordings are WAV "
                                   + "files, which Xournal++ plays but which are large, or AAC files, which are small.")
                    }

                    Heading { text: qsTr("Devices") }
                    Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("Microphone") }
                    ComboBox {
                        Layout.fillWidth: true
                        enabled: dialog.audio.available
                        model: [qsTr("As the system")].concat(dialog.audio.inputDevices)
                        currentIndex: Math.max(dialog.audio.inputDevices.indexOf(dialog.audio.inputDevice) + 1, 0)
                        onActivated: index => dialog.audio.inputDevice = index === 0 ? "" : model[index]
                    }
                    Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("Level of the recording") }
                    Slider {
                        Layout.fillWidth: true
                        enabled: dialog.audio.available
                        from: 0
                        to: 1
                        value: dialog.audio.gain
                        onMoved: dialog.audio.gain = value
                    }
                    Label { Layout.fillWidth: dialog.narrow; wrapMode: Text.Wrap; text: qsTr("Playback") }
                    ComboBox {
                        Layout.fillWidth: true
                        enabled: dialog.audio.available
                        model: [qsTr("As the system")].concat(dialog.audio.outputDevices)
                        currentIndex: Math.max(dialog.audio.outputDevices.indexOf(dialog.audio.outputDevice) + 1, 0)
                        onActivated: index => dialog.audio.outputDevice = index === 0 ? "" : model[index]
                    }

                    Heading { text: qsTr("Playback") }
                    Label {
                        Layout.fillWidth: dialog.narrow
                        wrapMode: Text.Wrap
                        text: qsTr("Default Seek Time (in seconds)")
                    }
                    SpinBox {
                        from: 1
                        to: 120
                        editable: true
                        value: dialog.audio.seekTime
                        onValueModified: dialog.audio.seekTime = value
                    }
                }
            }
        }
    }

    FolderDialog {
        id: audioFolderDialog

        title: qsTr("Folder of the audio recordings")
        onAccepted: dialog.audio.folder = selectedFolder
    }

    FileDialog {
        id: templateDialog

        title: qsTr("LaTeX template")
        nameFilters: [qsTr("LaTeX files (*.tex)"), qsTr("All files (*)")]
        onAccepted: dialog.app.latexTemplate = selectedFile.toString().replace(/^file:\/\//, "")
    }

    ColorDialog {
        id: buttonColorDialog

        property int button: 0

        title: qsTr("Colour of the button")
        onAccepted: dialog.canvas.setButtonOptions(button, { color: selectedColor })
    }

    ColorDialog {
        id: colorDialog

        property var apply: color => {}

        options: ColorDialog.ShowAlphaChannel
        onAccepted: apply(selectedColor)
    }

    ColorDialog {
        id: canvasColorDialog

        title: qsTr("Colour around the pages")
        onAccepted: dialog.canvas.canvasColor = selectedColor
    }
}
