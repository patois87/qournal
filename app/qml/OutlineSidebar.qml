pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Qournal

// The outline of the PDF: its entries lead to their pages
Pane {
    id: sidebar

    required property PageCanvas canvas

    padding: 0

    Label {
        anchors.centerIn: parent
        width: parent.width - 16
        visible: list.count === 0
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        text: sidebar.canvas.pdfPageCount > 0 ? qsTr("The PDF has no outline") : qsTr("The document has no PDF")
    }

    ListView {
        id: list

        anchors.fill: parent
        clip: true
        model: sidebar.canvas.pdfOutline

        ScrollBar.vertical: ScrollBar {}

        delegate: ItemDelegate {
            required property var modelData

            width: list.width
            leftPadding: 8 + 12 * modelData.level
            text: modelData.title
            onClicked: sidebar.canvas.goToPdfPage(modelData.page)

            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: modelData.title
        }
    }
}
