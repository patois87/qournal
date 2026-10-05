pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qournal

// The layers of the current page, the top one first, with buttons to arrange them
Pane {
    id: sidebar

    required property PageCanvas canvas
    readonly property int count: canvas.layers.length

    padding: 0

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ListView {
            id: list

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            // One entry more than there are layers: the last one is the background
            model: sidebar.count + 1

            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: entry

                required property int index
                // The list shows the top layer first
                readonly property int layerIndex: sidebar.count - 1 - index
                readonly property bool isBackground: layerIndex < 0
                readonly property var info: isBackground ? null : sidebar.canvas.layers[layerIndex]

                width: list.width
                highlighted: !isBackground && layerIndex === sidebar.canvas.currentLayer
                onClicked: {
                    if (!isBackground) {
                        sidebar.canvas.currentLayer = layerIndex
                    }
                }
                onDoubleClicked: {
                    if (!isBackground) {
                        renameDialog.openFor(layerIndex, info.name)
                    }
                }

                contentItem: RowLayout {
                    spacing: 4

                    CheckBox {
                        checked: entry.isBackground ? sidebar.canvas.backgroundVisible : (entry.info?.visible ?? true)
                        onToggled: {
                            if (entry.isBackground) {
                                sidebar.canvas.backgroundVisible = checked
                            } else {
                                sidebar.canvas.setLayerVisible(entry.layerIndex, checked)
                            }
                        }

                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Show or hide")
                    }
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: entry.isBackground ? qsTr("Background") : (entry.info?.name ?? "")
                        font.bold: entry.highlighted
                    }
                }
            }
        }

        ToolSeparator {
            Layout.fillWidth: true
            orientation: Qt.Horizontal
            padding: 0
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.margins: 2
            columns: 3
            rowSpacing: 0
            columnSpacing: 0

            ToolButton {
                Layout.fillWidth: true
                text: "+"
                onClicked: sidebar.canvas.addLayer()

                ToolTip.visible: hovered
                ToolTip.text: qsTr("New layer above this one")
            }
            ToolButton {
                Layout.fillWidth: true
                text: "×2"
                onClicked: sidebar.canvas.duplicateLayer(sidebar.canvas.currentLayer)

                ToolTip.visible: hovered
                ToolTip.text: qsTr("Duplicate layer")
            }
            ToolButton {
                Layout.fillWidth: true
                text: "×"
                enabled: sidebar.count > 1
                onClicked: sidebar.canvas.deleteLayer(sidebar.canvas.currentLayer)

                ToolTip.visible: hovered
                ToolTip.text: qsTr("Delete layer")
            }
            ToolButton {
                Layout.fillWidth: true
                text: "↑"
                enabled: sidebar.canvas.currentLayer < sidebar.count - 1
                onClicked: sidebar.canvas.moveLayer(sidebar.canvas.currentLayer, sidebar.canvas.currentLayer + 1)

                ToolTip.visible: hovered
                ToolTip.text: qsTr("Move layer up")
            }
            ToolButton {
                Layout.fillWidth: true
                text: "↓"
                enabled: sidebar.canvas.currentLayer > 0
                onClicked: sidebar.canvas.moveLayer(sidebar.canvas.currentLayer, sidebar.canvas.currentLayer - 1)

                ToolTip.visible: hovered
                ToolTip.text: qsTr("Move layer down")
            }
            ToolButton {
                Layout.fillWidth: true
                text: "+↓"
                enabled: sidebar.canvas.currentLayer > 0
                onClicked: sidebar.canvas.mergeLayerDown(sidebar.canvas.currentLayer)

                ToolTip.visible: hovered
                ToolTip.text: qsTr("Merge with the layer below")
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 2
            spacing: 0

            ToolButton {
                Layout.fillWidth: true
                text: qsTr("Show all")
                onClicked: sidebar.canvas.setAllLayersVisible(true)
            }
            ToolButton {
                Layout.fillWidth: true
                text: qsTr("Hide all")
                onClicked: sidebar.canvas.setAllLayersVisible(false)
            }
        }
    }

    Dialog {
        id: renameDialog

        property int layerIndex: -1

        function openFor(index, name) {
            layerIndex = index
            nameField.text = name
            open()
            nameField.forceActiveFocus()
            nameField.selectAll()
        }

        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        Overlay.modal: ModalDim {}
        title: qsTr("Rename layer")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: sidebar.canvas.renameLayer(layerIndex, nameField.text)

        TextField {
            id: nameField

            width: 240
            onAccepted: renameDialog.accept()
        }
    }
}
