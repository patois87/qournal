pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qournal

// Paper format and background of a page
Dialog {
    id: dialog

    required property PageCanvas canvas
    property int page: 0

    /// The unit the sizes are shown in, as in the settings of Xournal++: "cm", "mm", "in" or "pt"
    property string unit: "cm"
    // Sizes are kept in points (1/72 inch)
    readonly property real pointsPerUnit: unit === "mm" ? 72 / 25.4 : unit === "in" ? 72 : unit === "pt" ? 1 : 72 / 2.54
    readonly property var formats: [
        { name: "A3", width: 841.88976, height: 1190.5512 },
        { name: "A4", width: 595.27559, height: 841.88976 },
        { name: "A5", width: 419.52756, height: 595.27559 },
        { name: qsTr("US Letter"), width: 612, height: 792 },
        { name: qsTr("US Legal"), width: 612, height: 1008 }
    ]
    readonly property var paperColors: ["#ffffff", "#fef8c9", "#fabebe", "#dcf6c1", "#d4e2f0", "#ffc080", "#434343",
                                         "#000000"]

    property real pageWidth: 595.27559
    property real pageHeight: 841.88976
    property color paperColor: "#ffffff"
    property var pageTypes: []
    property bool pdfBackground: false
    property bool imageBackground: false

    function openFor(pageIndex) {
        page = pageIndex
        const properties = canvas.pageProperties(pageIndex)
        pageWidth = properties.width
        pageHeight = properties.height
        pageTypes = canvas.pageTypes()
        imageBackground = properties.type === "pixmap"
        pdfBackground = properties.type === "pdf"
        pdfPageBox.value = pdfBackground ? properties.pdfPage : 1
        allPages.checked = false

        if (properties.type === "solid") {
            paperColor = properties.color
            let index = pageTypes.findIndex(type => type.style === properties.style
                                                    && type.config === properties.config)
            if (index < 0) {
                // A ruling with its own configuration: offer it as it is
                pageTypes = pageTypes.concat([{ name: qsTr("Current (%1)").arg(properties.style),
                                                style: properties.style, config: properties.config }])
                index = pageTypes.length - 1
            }
            styleBox.currentIndex = index
        } else {
            paperColor = "#ffffff"
            styleBox.currentIndex = 0
        }
        keepBackground.checked = properties.type !== "solid"
        updateFormat()
        open()
    }

    // Selects the format that matches the size, in either orientation
    function updateFormat() {
        const matches = (a, b) => Math.abs(a - b) < 0.5
        const fits = (format, width, height) => matches(format.width, width) && matches(format.height, height)
        const index = formats.findIndex(format => fits(format, pageWidth, pageHeight)
                                                  || fits(format, pageHeight, pageWidth))
        formatBox.currentIndex = index >= 0 ? index : formats.length
        widthField.text = Number(pageWidth / pointsPerUnit).toLocaleString(Qt.locale(), "f", 2)
        heightField.text = Number(pageHeight / pointsPerUnit).toLocaleString(Qt.locale(), "f", 2)
    }

    function setSize(width, height) {
        if (width >= 10 && height >= 10) {
            pageWidth = width
            pageHeight = height
        }
        updateFormat()
    }

    anchors.centerIn: parent
    width: Math.min(parent.width - 32, 480 * Math.max(1, font.pixelSize / 13))
    modal: true
    Overlay.modal: ModalDim {}
    title: qsTr("Page %1: format and background").arg(page + 1)
    standardButtons: Dialog.Ok | Dialog.Cancel

    onAccepted: {
        let properties = {}
        if (pdfButton.checked) {
            // The page takes the size of the PDF page
            properties = { type: "pdf", pdfPage: pdfPageBox.value }
        } else {
            properties = { width: pageWidth, height: pageHeight }
            if (!keepBackground.checked) {
                const type = pageTypes[styleBox.currentIndex]
                properties.type = "solid"
                properties.color = paperColor
                properties.style = type.style
                properties.config = type.config
            }
        }
        canvas.setPageProperties(page, properties, allPages.checked && !pdfButton.checked)
    }

    GridLayout {
        width: parent.width
        columns: 2
        columnSpacing: 12

        Label { text: qsTr("Paper format") }
        FitComboBox {
            id: formatBox

            Layout.fillWidth: true
            enabled: !pdfButton.checked
            model: dialog.formats.map(format => format.name).concat([qsTr("Custom")])
            onActivated: index => {
                if (index < dialog.formats.length) {
                    const format = dialog.formats[index]
                    const landscape = dialog.pageWidth > dialog.pageHeight
                    dialog.setSize(landscape ? format.height : format.width, landscape ? format.width : format.height)
                }
            }
        }

        Label { text: qsTr("Size (%1)").arg(dialog.unit) }
        RowLayout {
            enabled: !pdfButton.checked

            TextField {
                id: widthField

                Layout.preferredWidth: 80
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                validator: DoubleValidator {
                    bottom: 14 / dialog.pointsPerUnit
                    top: 14000 / dialog.pointsPerUnit
                    decimals: 2
                }
                onEditingFinished: dialog.setSize(Number.fromLocaleString(Qt.locale(), text) * dialog.pointsPerUnit,
                                                  dialog.pageHeight)
            }
            Label { text: "×" }
            TextField {
                id: heightField

                Layout.preferredWidth: 80
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                validator: DoubleValidator {
                    bottom: 14 / dialog.pointsPerUnit
                    top: 14000 / dialog.pointsPerUnit
                    decimals: 2
                }
                onEditingFinished: dialog.setSize(dialog.pageWidth,
                                                  Number.fromLocaleString(Qt.locale(), text) * dialog.pointsPerUnit)
            }
            ToolButton {
                text: "↔"
                onClicked: dialog.setSize(dialog.pageHeight, dialog.pageWidth)

                ToolTip.visible: hovered
                ToolTip.text: qsTr("Swap width and height")
            }
        }

        // Only if there is something to choose from: the PDF or image of the page, or a page of the PDF
        Label {
            visible: backgroundChoice.visible
            text: qsTr("Background")
        }
        ColumnLayout {
            id: backgroundChoice

            visible: keepBackground.visible || pdfButton.visible

            RadioButton {
                id: keepBackground

                // PDF and image backgrounds stay as they are unless another background is chosen
                visible: dialog.pdfBackground || dialog.imageBackground
                text: dialog.pdfBackground ? qsTr("Keep the PDF page") : qsTr("Keep the image")
            }
            RadioButton {
                id: paperButton

                visible: keepBackground.visible || pdfButton.visible
                checked: !keepBackground.checked
                text: qsTr("Paper")
            }
            RadioButton {
                id: pdfButton

                visible: dialog.canvas.pdfPageCount > 0
                text: qsTr("Page of the PDF")
            }
        }

        Label {
            visible: pdfButton.checked
            text: qsTr("PDF page")
        }
        SpinBox {
            id: pdfPageBox

            visible: pdfButton.checked
            from: 1
            to: Math.max(dialog.canvas.pdfPageCount, 1)
            editable: true
        }

        Label {
            visible: !pdfButton.checked && !keepBackground.checked
            text: qsTr("Ruling")
        }
        FitComboBox {
            id: styleBox

            Layout.fillWidth: true
            visible: !pdfButton.checked && !keepBackground.checked
            model: dialog.pageTypes.map(type => type.name)
        }

        Label {
            visible: !pdfButton.checked && !keepBackground.checked
            text: qsTr("Paper colour")
        }
        Flow {
            Layout.fillWidth: true
            visible: !pdfButton.checked && !keepBackground.checked
            spacing: 6

            Repeater {
                model: dialog.paperColors

                Rectangle {
                    required property string modelData
                    readonly property bool selected: Qt.colorEqual(dialog.paperColor, modelData)

                    width: 28
                    height: 28
                    radius: 4
                    color: modelData
                    border.width: selected ? 3 : 1
                    border.color: selected ? dialog.palette.highlight : dialog.palette.mid

                    TapHandler { onTapped: dialog.paperColor = parent.modelData }
                }
            }
        }

        CheckBox {
            id: allPages

            Layout.columnSpan: 2
            enabled: !pdfButton.checked
            text: qsTr("Apply to all pages")
        }
    }
}
