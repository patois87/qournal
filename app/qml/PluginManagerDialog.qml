pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qournal

// Lists the plugins that were found and switches them on and off
Dialog {
    id: dialog

    required property PluginController plugins

    anchors.centerIn: parent
    width: Math.min(parent.width - 32, 600 * Math.max(1, font.pixelSize / 13))
    height: Math.min(parent.height - 32, 520 * Math.max(1, font.pixelSize / 13))
    modal: true
    Overlay.modal: ModalDim {}
    title: qsTr("Plugin manager")
    standardButtons: Dialog.Close

    ColumnLayout {
        anchors.fill: parent

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            visible: !dialog.plugins.available
            text: qsTr("This build cannot run plugins: it was built without Lua.")
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            visible: dialog.plugins.available && dialog.plugins.plugins.length === 0
            text: qsTr("No plugins were found.")
        }

        ListView {
            id: list

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            model: dialog.plugins.plugins
            ScrollBar.vertical: ScrollBar {}

            delegate: RowLayout {
                id: row

                required property var modelData

                width: ListView.view.width - 12
                spacing: 8

                CheckBox {
                    Layout.alignment: Qt.AlignTop
                    enabled: dialog.plugins.available && (row.modelData.valid || row.modelData.enabled)
                    checked: row.modelData.enabled
                    onToggled: dialog.plugins.setPluginEnabled(row.modelData.name, checked)
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Label {
                        Layout.fillWidth: true
                        font.bold: true
                        elide: Text.ElideRight
                        text: row.modelData.version !== "" ? qsTr("%1 (version %2)").arg(row.modelData.name)
                                                                                   .arg(row.modelData.version)
                                                           : row.modelData.name
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        visible: text !== ""
                        text: row.modelData.description
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        visible: row.modelData.author !== ""
                        opacity: 0.7
                        text: qsTr("By %1").arg(row.modelData.author)
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        visible: row.modelData.error !== ""
                        color: "#d03030"
                        text: row.modelData.error
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            opacity: 0.7
            text: qsTr("Plugins are folders with a plugin.ini and a Lua file. They are looked for next to the "
                       + "application and in:\n%1").arg(dialog.plugins.userFolder)
        }
    }
}
