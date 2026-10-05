pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qournal

// Previews of all pages, with buttons to arrange them
Pane {
    id: sidebar

    required property PageCanvas canvas
    /// Where the numbers of the pages are, as in Xournal++: "below", "circle" or "square" (over the preview), "none"
    property string numbers: "below"
    // The page that is being dragged and the place it is over, -1 if none
    property int dragFrom: -1
    property int dragTo: -1

    padding: 0

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ListView {
            id: list

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            topMargin: 8
            bottomMargin: 8
            model: sidebar.canvas.pageCount
            currentIndex: sidebar.canvas.currentPage
            highlightFollowsCurrentItem: false
            // On electronic paper nothing glides or bounces: each step of it would be drawn
            boundsBehavior: sidebar.canvas.einkMode ? Flickable.StopAtBounds : Flickable.DragAndOvershootBounds
            flickDeceleration: sidebar.canvas.einkMode ? 100000 : 1500
            onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)

            ScrollBar.vertical: ScrollBar {}

            delegate: Item {
                id: entry

                required property int index
                readonly property bool selected: index === sidebar.canvas.currentPage
                // Depends on the page count, so that it is read again when pages are added, removed or moved
                readonly property size pageSize: sidebar.canvas.pageCount > 0 ? sidebar.canvas.pageSize(index)
                                                                                : Qt.size(1, 1)
                readonly property real previewWidth: Math.max(list.width - 40, 16)
                readonly property real previewHeight: Math.min(
                    previewWidth * pageSize.height / Math.max(pageSize.width, 1), previewWidth * 2)

                width: list.width
                height: previewHeight + (sidebar.numbers === "below" ? label.height + 14 : 10)

                Rectangle {
                    anchors.centerIn: preview
                    width: preview.width + 6
                    height: preview.height + 6
                    color: "transparent"
                    border.width: entry.selected ? 3 : 1
                    // The colour of selections, as in Xournal++
                    border.color: entry.selected ? sidebar.canvas.selectionColor : sidebar.palette.mid
                }

                PagePreview {
                    id: preview

                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 5
                    // The page keeps its aspect ratio within the available space
                    width: Math.min(entry.previewWidth,
                                    entry.previewHeight * entry.pageSize.width / Math.max(entry.pageSize.height, 1))
                    height: entry.previewHeight
                    canvas: sidebar.canvas
                    page: entry.index
                }

                Label {
                    id: label

                    anchors.top: preview.bottom
                    anchors.topMargin: 5
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: sidebar.numbers === "below"
                    text: entry.index + 1
                    font.bold: entry.selected
                }

                // The number on the preview, in a circle or a square at its lower right corner
                Rectangle {
                    anchors.right: preview.right
                    anchors.bottom: preview.bottom
                    anchors.margins: 4
                    visible: sidebar.numbers === "circle" || sidebar.numbers === "square"
                    width: Math.max(overLabel.implicitWidth + 8, height)
                    height: overLabel.implicitHeight + 4
                    radius: sidebar.numbers === "circle" ? height / 2 : 2
                    color: entry.selected ? sidebar.canvas.selectionColor : sidebar.palette.mid

                    Label {
                        id: overLabel

                        anchors.centerIn: parent
                        text: entry.index + 1
                        color: "white"
                        font.bold: entry.selected
                    }
                }

                TapHandler { onTapped: sidebar.canvas.currentPage = entry.index }

                // Dragging a page up or down moves it to the place where it is dropped
                DragHandler {
                    id: pageDrag

                    target: null
                    xAxis.enabled: false
                    onActiveChanged: {
                        if (active) {
                            sidebar.dragFrom = entry.index
                            sidebar.canvas.currentPage = entry.index
                        } else if (sidebar.dragFrom >= 0) {
                            if (sidebar.dragTo >= 0 && sidebar.dragTo !== sidebar.dragFrom) {
                                sidebar.canvas.movePage(sidebar.dragFrom, sidebar.dragTo)
                            }
                            sidebar.dragFrom = -1
                            sidebar.dragTo = -1
                        }
                    }
                    onCentroidChanged: {
                        if (active) {
                            const pos = entry.mapToItem(list.contentItem, centroid.position)
                            const index = list.indexAt(list.width / 2, pos.y)
                            sidebar.dragTo = index >= 0 ? index : sidebar.dragFrom
                        }
                    }
                }

                // Where the page that is dragged would go
                Rectangle {
                    visible: sidebar.dragFrom >= 0 && sidebar.dragTo === entry.index
                             && sidebar.dragTo !== sidebar.dragFrom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    y: sidebar.dragTo > sidebar.dragFrom ? parent.height - 1 : -3
                    height: 4
                    color: sidebar.palette.highlight
                }
            }
        }

        ToolSeparator {
            Layout.fillWidth: true
            orientation: Qt.Horizontal
            padding: 0
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 2
            spacing: 0

            ToolButton {
                Layout.fillWidth: true
                text: "↑"
                enabled: sidebar.canvas.currentPage > 0
                onClicked: sidebar.canvas.movePage(sidebar.canvas.currentPage, sidebar.canvas.currentPage - 1)

                ToolTip.visible: hovered
                ToolTip.text: qsTr("Move page upwards")
            }
            ToolButton {
                Layout.fillWidth: true
                text: "↓"
                enabled: sidebar.canvas.currentPage < sidebar.canvas.pageCount - 1
                onClicked: sidebar.canvas.movePage(sidebar.canvas.currentPage, sidebar.canvas.currentPage + 1)

                ToolTip.visible: hovered
                ToolTip.text: qsTr("Move page downwards")
            }
            ToolButton {
                Layout.fillWidth: true
                text: "+"
                onClicked: sidebar.canvas.insertPage(sidebar.canvas.currentPage + 1)

                ToolTip.visible: hovered
                ToolTip.text: qsTr("New Page After")
            }
            ToolButton {
                Layout.fillWidth: true
                text: "×2"
                onClicked: sidebar.canvas.duplicatePage(sidebar.canvas.currentPage)

                ToolTip.visible: hovered
                ToolTip.text: qsTr("Duplicate page")
            }
            ToolButton {
                Layout.fillWidth: true
                text: "×"
                enabled: sidebar.canvas.pageCount > 1
                onClicked: sidebar.canvas.deletePage(sidebar.canvas.currentPage)

                ToolTip.visible: hovered
                ToolTip.text: qsTr("Delete page")
            }
        }
    }
}
