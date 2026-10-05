pragma ComponentBehavior: Bound

import QtQuick
import Qournal

// The editor for the text that is being written, on top of the canvas. It lays out the text in the units the
// canvas renders it with; the matrix of the canvas puts it at the place, size and rotation it has on the page.
Item {
    id: editor

    required property PageCanvas canvas
    property bool wasEditing: false
    // One pixel of the screen in the units of the editor
    readonly property real unit: 1 / Math.max(Math.hypot(canvas.textEditMatrix.m11, canvas.textEditMatrix.m21), 1e-6)

    visible: canvas.textEditing

    Connections {
        target: editor.canvas

        function onTextEditChanged() {
            if (editor.canvas.textEditing && !editor.wasEditing) {
                edit.text = editor.canvas.textEditText
                edit.cursorPosition = edit.length
                edit.forceActiveFocus()
            }
            if (editor.canvas.textEditing) {
                // The lines keep the distance they will have on the page, also after a change of the font
                editor.canvas.prepareTextDocument(edit.textDocument)
            }
            editor.wasEditing = editor.canvas.textEditing
        }
    }

    TextEdit {
        id: edit

        width: editor.canvas.textEditWrap >= 0 ? editor.canvas.textEditWrap : Math.max(implicitWidth, 40)
        transform: Matrix4x4 { matrix: editor.canvas.textEditMatrix }
        font: editor.canvas.textEditFont
        color: editor.canvas.textEditColor
        textFormat: TextEdit.PlainText
        wrapMode: editor.canvas.textEditWrap >= 0 ? TextEdit.WordWrap : TextEdit.NoWrap
        horizontalAlignment: editor.canvas.textEditJustify && editor.canvas.textEditWrap >= 0
                             ? TextEdit.AlignJustify
                             : editor.canvas.textEditAlign === "center" ? TextEdit.AlignHCenter
                             : editor.canvas.textEditAlign === "right" ? TextEdit.AlignRight : TextEdit.AlignLeft
        selectByMouse: true
        cursorDelegate: Rectangle {
            width: 2 * editor.unit
            color: edit.color
            visible: edit.cursorVisible
        }

        onTextChanged: {
            if (editor.canvas.textEditing) {
                editor.canvas.textEditText = text
            }
        }
        // Escape ends the editing and keeps the text, as in Xournal++
        Keys.onEscapePressed: editor.canvas.finishTextEdit()
        // Spaces instead of a tab, if the settings say so (Xournal++: numberOfSpacesForTab)
        Keys.onTabPressed: event => {
            if (editor.canvas.input.tabSpaces > 0) {
                edit.remove(edit.selectionStart, edit.selectionEnd)
                edit.insert(edit.cursorPosition, " ".repeat(editor.canvas.input.tabSpaces))
            } else {
                event.accepted = false
            }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -4 * editor.unit
            color: "transparent"
            border.width: editor.unit
            border.color: "#808080"
        }

        // Dragging the right border sets the width at which the lines wrap
        Rectangle {
            id: wrapHandle

            x: parent.width + 4 * editor.unit - width / 2
            y: (parent.height - height) / 2
            width: 8 * editor.unit
            height: 24 * editor.unit
            color: "#ffffff"
            border.width: editor.unit
            border.color: "#808080"

            MouseArea {
                property real pressX: 0
                property real startWidth: 0

                anchors.fill: parent
                anchors.margins: -6 * editor.unit
                cursorShape: Qt.SizeHorCursor
                onPressed: mouse => {
                    pressX = mapToItem(edit, mouse.x, mouse.y).x
                    startWidth = edit.width
                }
                onPositionChanged: mouse => {
                    const x = mapToItem(edit, mouse.x, mouse.y).x
                    editor.canvas.setTextEditWrap(Math.max(startWidth + x - pressX, 50))
                }
                // A double click lets the text take its own width again
                onDoubleClicked: editor.canvas.setTextEditWrap(-1)
            }
        }
    }
}
