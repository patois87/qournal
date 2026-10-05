pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Qournal

// Chooses the toolbar configuration, and the items of its toolbars and their order
Dialog {
    id: dialog

    /// The main window: allItems and itemName()
    required property var app
    required property ToolbarModel toolbars
    /// The toolbars of a configuration, as the keys of ToolbarModel.bars
    readonly property var barNames: [
        { bar: "top1", name: qsTr("Top") },
        { bar: "top2", name: qsTr("Top, second row") },
        { bar: "left1", name: qsTr("Left") },
        { bar: "left2", name: qsTr("Left, second column") },
        { bar: "right1", name: qsTr("Right") },
        { bar: "right2", name: qsTr("Right, second column") },
        { bar: "bottom1", name: qsTr("Bottom") },
        { bar: "bottom2", name: qsTr("Bottom, second row") },
        { bar: "float1", name: qsTr("Floating toolbox") }
    ]
    readonly property string bar: barNames[barBox.currentIndex].bar
    readonly property var items: toolbars.bars[bar] ?? []
    /// What is in any toolbar of the configuration
    readonly property var usedItems: app.allBarItems

    function setItems(items) {
        toolbars.setItems(bar, items)
    }

    function add(id) {
        const items = dialog.items.slice()
        const at = currentList.currentIndex >= 0 ? currentList.currentIndex + 1 : items.length
        items.splice(at, 0, id)
        setItems(items)
        currentList.currentIndex = at
    }

    function remove(index) {
        const items = dialog.items.slice()
        items.splice(index, 1)
        setItems(items)
        currentList.currentIndex = Math.min(index, items.length - 1)
    }

    function move(index, to) {
        if (index < 0 || to < 0 || to >= items.length) {
            return
        }
        const items = dialog.items.slice()
        items.splice(to, 0, items.splice(index, 1)[0])
        setItems(items)
        currentList.currentIndex = to
    }

    /// On a phone: less next to each other
    readonly property bool narrow: width < 34 * font.pixelSize

    anchors.centerIn: parent
    width: Math.min(parent.width - 32, 680 * Math.max(1, font.pixelSize / 13))
    height: Math.min(parent.height - 32, 620 * Math.max(1, font.pixelSize / 13))
    modal: true
    Overlay.modal: ModalDim {}
    title: qsTr("Customize toolbars")
    standardButtons: Dialog.Close

    component ItemList: Frame {
        id: frame

        property alias model: list.model
        property alias currentIndex: list.currentIndex

        signal activated(int index)

        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredWidth: 100
        padding: 1

        ListView {
            id: list

            anchors.fill: parent
            clip: true
            currentIndex: -1
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: delegate

                required property int index
                required property string modelData

                width: ListView.view.width
                text: dialog.app.itemName(modelData)
                highlighted: ListView.isCurrentItem
                onClicked: ListView.view.currentIndex = index
                onDoubleClicked: frame.activated(index)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent

        // The configuration. In a narrow window its buttons are a row of their own
        GridLayout {
            Layout.fillWidth: true
            columns: dialog.narrow ? 4 : 5

            ComboBox {
                id: configBox

                Layout.fillWidth: true
                Layout.columnSpan: dialog.narrow ? 4 : 1
                model: dialog.toolbars.configs
                textRole: "name"
                valueRole: "id"
                currentIndex: indexOfValue(dialog.toolbars.shown)
                onActivated: {
                    dialog.toolbars.current = currentValue
                    currentList.currentIndex = -1
                }

                ToolTip.visible: hovered
                ToolTip.text: qsTr("Toolbar configuration")
            }
            Button {
                Layout.fillWidth: dialog.narrow
                text: qsTr("Copy")
                onClicked: dialog.toolbars.copyShown()
            }
            Button {
                Layout.fillWidth: dialog.narrow
                text: qsTr("Rename…")
                enabled: !dialog.toolbars.shownPredefined
                onClicked: {
                    nameField.text = dialog.toolbars.shownName
                    renameDialog.open()
                }
            }
            Button {
                Layout.fillWidth: dialog.narrow
                text: qsTr("Delete")
                enabled: !dialog.toolbars.shownPredefined
                onClicked: dialog.toolbars.removeShown()
            }
            Button {
                Layout.fillWidth: dialog.narrow
                text: qsTr("Import…")
                onClicked: {
                    const theirs = dialog.toolbars.xournalppFile()
                    if (theirs.toString() !== "") {
                        importDialog.currentFolder = theirs.toString().replace(/\/[^\/]*$/, "")
                    }
                    importDialog.open()
                }

                ToolTip.visible: hovered
                ToolTip.text: qsTr("Take over the configurations of a toolbar.ini of Xournal++")
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            visible: dialog.toolbars.shownPredefined
            text: qsTr("This configuration comes with the application. Changing it makes a copy that is yours.")
        }
        Label {
            id: importResult

            Layout.fillWidth: true
            wrapMode: Text.Wrap
            visible: text !== ""
        }

        ComboBox {
            id: barBox

            Layout.fillWidth: true
            model: dialog.barNames.map(entry => entry.name)
            onActivated: currentList.currentIndex = -1
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                Label { text: qsTr("Available") }
                ItemList {
                    id: availableList

                    // Separators and spacers can be used several times
                    model: dialog.app.allItems.filter(id => id === "separator" || id === "spacer"
                                                            || !dialog.usedItems.includes(id))
                    onActivated: index => dialog.add(model[index])
                }
            }

            ColumnLayout {
                Layout.fillWidth: false

                Button {
                    text: dialog.narrow ? "→" : qsTr("Add →")
                    Accessible.name: qsTr("Add →")
                    enabled: availableList.currentIndex >= 0
                    onClicked: dialog.add(availableList.model[availableList.currentIndex])
                }
                Button {
                    text: dialog.narrow ? "←" : qsTr("← Remove")
                    Accessible.name: qsTr("← Remove")
                    enabled: currentList.currentIndex >= 0
                    onClicked: dialog.remove(currentList.currentIndex)
                }
                Button {
                    text: dialog.narrow ? "↑" : qsTr("Up")
                    Accessible.name: qsTr("Up")
                    enabled: currentList.currentIndex > 0
                    onClicked: dialog.move(currentList.currentIndex, currentList.currentIndex - 1)
                }
                Button {
                    text: dialog.narrow ? "↓" : qsTr("Down")
                    Accessible.name: qsTr("Down")
                    enabled: currentList.currentIndex >= 0 && currentList.currentIndex < dialog.items.length - 1
                    onClicked: dialog.move(currentList.currentIndex, currentList.currentIndex + 1)
                }
            }

            ColumnLayout {
                Label { text: qsTr("In this toolbar") }
                ItemList {
                    id: currentList

                    model: dialog.items
                    onActivated: index => dialog.remove(index)
                }
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("The floating toolbox opens at the pointer. A button of the pen or of the mouse can be given "
                       + "this task in the preferences.")
        }
    }

    Dialog {
        id: renameDialog

        anchors.centerIn: parent
        modal: true
        Overlay.modal: ModalDim {}
        title: qsTr("Name of the configuration")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: dialog.toolbars.renameShown(nameField.text)

        TextField {
            id: nameField

            width: 280
            onAccepted: renameDialog.accept()
        }
    }

    FileDialog {
        id: importDialog

        title: qsTr("Toolbar configurations of Xournal++")
        nameFilters: [qsTr("Toolbar configurations (toolbar.ini *.ini)"), qsTr("All files (*)")]
        onAccepted: {
            const count = dialog.toolbars.importFile(selectedFile)
            importResult.text = count > 0 ? qsTr("%n configuration(s) taken over.", "", count)
                                          : qsTr("The file has no configurations that are not here already.")
        }
    }
}
