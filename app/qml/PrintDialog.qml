pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qournal

// Chooses the printer and the pages
Dialog {
    id: dialog

    required property PageCanvas canvas
    property var printerNames: []

    anchors.centerIn: parent
    width: Math.min(parent.width - 32, 440 * Math.max(1, font.pixelSize / 13))
    modal: true
    Overlay.modal: ModalDim {}
    title: qsTr("Print")

    onAboutToShow: printerNames = canvas.printers()
    onAccepted: canvas.printWith({
        printer: printerNames.length > 0 ? printerNames[printerBox.currentIndex] : "",
        pages: pagesField.text,
        copies: copiesBox.value,
        duplex: ["none", "longSide", "shortSide"][duplexBox.currentIndex],
        grayscale: grayscaleBox.checked,
        paper: paperBox.currentIndex === 1 ? "printer" : "page"
    })

    GridLayout {
        width: parent.width
        columns: 2
        columnSpacing: 12

        Label {
            Layout.columnSpan: 2
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            visible: dialog.printerNames.length === 0
            text: dialog.canvas.canPrint()
                  ? qsTr("No printer was found. The document can be exported as PDF instead.")
                  : qsTr("This build cannot print. The document can be exported as PDF instead.")
        }

        Label { text: qsTr("Printer") }
        ComboBox {
            id: printerBox

            Layout.fillWidth: true
            enabled: dialog.printerNames.length > 0
            model: dialog.printerNames
        }

        Label { text: qsTr("Pages") }
        TextField {
            id: pagesField

            Layout.fillWidth: true
            placeholderText: qsTr("All; or e.g. 1-3,5,7-")
        }

        Label { text: qsTr("Copies") }
        SpinBox {
            id: copiesBox

            from: 1
            to: 999
            editable: true
            value: 1
        }

        Label { text: qsTr("Paper") }
        ComboBox {
            id: paperBox

            Layout.fillWidth: true
            model: [qsTr("Of the size of the pages"), qsTr("That of the printer; the pages are fitted")]
        }

        Label { text: qsTr("Both sides") }
        ComboBox {
            id: duplexBox

            Layout.fillWidth: true
            model: [qsTr("One side only"), qsTr("Turned over the long side"), qsTr("Turned over the short side")]
        }

        CheckBox {
            id: grayscaleBox

            Layout.columnSpan: 2
            text: qsTr("In shades of grey")
        }
    }

    footer: DialogButtonBox {
        Button {
            text: qsTr("Print")
            enabled: dialog.printerNames.length > 0
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
        Button {
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }
}
