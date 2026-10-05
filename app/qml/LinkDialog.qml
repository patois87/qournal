pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qournal

// Creates a link, or changes, opens or removes the one the link tool was used on
Dialog {
    id: dialog

    required property PageCanvas canvas
    property bool existing: false

    function openFor(text, url, isExisting) {
        existing = isExisting
        textField.text = text
        urlField.text = url
        open()
        urlField.forceActiveFocus()
    }

    anchors.centerIn: parent
    width: Math.min(parent.width - 32, 480 * Math.max(1, font.pixelSize / 13))
    modal: true
    Overlay.modal: ModalDim {}
    title: existing ? qsTr("Edit link") : qsTr("New link")

    onAccepted: canvas.applyLink(textField.text, urlField.text)

    GridLayout {
        width: parent.width
        columns: 2
        columnSpacing: 12

        Label { text: qsTr("Address") }
        TextField {
            id: urlField

            Layout.fillWidth: true
            placeholderText: "https://"
            inputMethodHints: Qt.ImhUrlCharactersOnly
            onAccepted: dialog.accept()
        }
        Label { text: qsTr("Text") }
        TextField {
            id: textField

            Layout.fillWidth: true
            placeholderText: qsTr("The address, if left empty")
            onAccepted: dialog.accept()
        }
    }

    footer: DialogButtonBox {
        Button {
            text: qsTr("Open")
            visible: dialog.existing
            enabled: urlField.text.trim() !== ""
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: Qt.openUrlExternally(urlField.text.trim())
        }
        Button {
            text: qsTr("Remove")
            visible: dialog.existing
            DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
            onClicked: {
                dialog.canvas.removeLink()
                dialog.close()
            }
        }
        Button {
            text: qsTr("OK")
            enabled: urlField.text.trim() !== ""
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
        Button {
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }
}
