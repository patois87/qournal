pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Qournal

// Chooses what is exported and how, then asks for the file
Dialog {
    id: dialog

    required property PageCanvas canvas
    /// The name the file dialog suggests, see PageCanvas::nameFromPattern()
    property string namePattern: "%{name}"
    property url folder
    readonly property var formats: canvas.canExportSvg() ? ["pdf", "png", "svg"] : ["pdf", "png"]
    readonly property string format: formats[formatBox.currentIndex]

    anchors.centerIn: parent
    width: Math.min(parent.width - 32, 480 * Math.max(1, font.pixelSize / 13))
    modal: true
    Overlay.modal: ModalDim {}
    title: qsTr("Export")
    standardButtons: Dialog.Ok | Dialog.Cancel

    onAccepted: {
        fileDialog.currentFolder = folder
        fileDialog.selectedFile = folder + "/" + canvas.nameFromPattern(namePattern) + "." + format
        fileDialog.open()
    }

    GridLayout {
        width: parent.width
        columns: 2
        columnSpacing: 12

        Label { text: qsTr("Format") }
        ComboBox {
            id: formatBox

            Layout.fillWidth: true
            model: dialog.formats.map(format => format.toUpperCase())
        }

        Label { text: qsTr("Pages") }
        TextField {
            id: pagesField

            Layout.fillWidth: true
            placeholderText: qsTr("All; or e.g. 1-3,5,7-")
        }

        Label { text: qsTr("Layers") }
        TextField {
            id: layersField

            Layout.fillWidth: true
            placeholderText: qsTr("The visible ones; or e.g. 1-2")
        }

        Label { text: qsTr("Background") }
        ComboBox {
            id: backgroundBox

            Layout.fillWidth: true
            model: [qsTr("As shown"), qsTr("Without ruling"), qsTr("None")]
        }

        WrapCheckBox {
            id: progressiveBox

            Layout.columnSpan: 2
            text: qsTr("One page per layer, each with the layers up to it")
        }

        Label {
            visible: dialog.format === "png"
            text: qsTr("Resolution (dpi)")
        }
        SpinBox {
            id: dpiBox

            visible: dialog.format === "png"
            from: 30
            to: 1200
            stepSize: 50
            value: 300
            editable: true
        }

        Label {
            Layout.columnSpan: 2
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            visible: dialog.format !== "png" && dialog.canvas.pdfPageCount > 0
            text: qsTr("The pages of the background PDF are exported as images: their text cannot be selected "
                       + "in the exported file.")
        }
    }

    FileDialog {
        id: fileDialog

        title: qsTr("Export to")
        fileMode: FileDialog.SaveFile
        defaultSuffix: dialog.format
        nameFilters: [qsTr("%1 files (*.%2)").arg(dialog.format.toUpperCase()).arg(dialog.format)]
        onAccepted: dialog.canvas.exportDocument(selectedFile, {
            format: dialog.format,
            pages: pagesField.text,
            layers: layersField.text,
            background: ["all", "noRuling", "none"][backgroundBox.currentIndex],
            progressive: progressiveBox.checked,
            dpi: dpiBox.value
        })
    }
}
