pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qournal

// The audio recordings of the document: each with the pages that refer to it, at the moment they do
Dialog {
    id: dialog

    required property PageCanvas canvas
    required property AudioController audio
    property var recordings: []

    function formatTime(milliseconds) {
        const seconds = Math.floor(milliseconds / 1000)
        const minutes = Math.floor(seconds / 60)
        return minutes + ":" + String(seconds % 60).padStart(2, "0")
    }

    anchors.centerIn: parent
    width: Math.min(parent.width - 32, 560 * Math.max(1, font.pixelSize / 13))
    height: Math.min(parent.height - 32, 480 * Math.max(1, font.pixelSize / 13))
    modal: true
    Overlay.modal: ModalDim {}
    title: qsTr("Recordings of the document")
    standardButtons: Dialog.Close
    onAboutToShow: recordings = canvas.recordings()

    Label {
        anchors.centerIn: parent
        visible: dialog.recordings.length === 0
        text: qsTr("Nothing in this document refers to a recording.")
    }

    ListView {
        anchors.fill: parent
        clip: true
        spacing: 12
        model: dialog.recordings
        ScrollBar.vertical: ScrollBar {}

        delegate: ColumnLayout {
            id: entry

            required property var modelData
            readonly property string path: dialog.audio.locate(modelData.file, dialog.canvas.filePath)

            width: ListView.view.width - 12
            spacing: 4

            RowLayout {
                Layout.fillWidth: true

                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    font.bold: true
                    text: entry.modelData.file
                }
                Label {
                    text: qsTr("%n element(s)", "", entry.modelData.elements)
                }
                Button {
                    text: qsTr("Play")
                    enabled: entry.path !== ""
                    onClicked: dialog.audio.play(entry.modelData.file, 0, dialog.canvas.filePath)
                }
            }
            Label {
                visible: entry.path === ""
                text: qsTr("The file of the recording was not found.")
                color: palette.placeholderText
            }
            // Where each page refers to the recording first; a click goes there and plays from that moment
            Flow {
                Layout.fillWidth: true
                spacing: 4

                Repeater {
                    model: entry.modelData.marks

                    Button {
                        required property var modelData

                        flat: true
                        text: qsTr("Page %1 at %2").arg(modelData.page + 1).arg(dialog.formatTime(modelData.timestamp))
                        onClicked: {
                            dialog.canvas.currentPage = modelData.page
                            if (entry.path !== "") {
                                dialog.audio.play(entry.modelData.file, modelData.timestamp, dialog.canvas.filePath)
                            }
                        }
                    }
                }
            }
        }
    }
}
