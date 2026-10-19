pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Qournal

// The editor for the text that is being written, on top of the canvas. It lays out the text in the units the
// canvas renders it with; the matrix of the canvas puts it at the place, size and rotation it has on the page.
Item {
    id: editor

    required property PageCanvas canvas
    property bool wasEditing: false
    // One pixel of the screen in the units of the editor
    readonly property real unit: 1 / Math.max(Math.hypot(canvas.textEditMatrix.m11, canvas.textEditMatrix.m21), 1e-6)
    // Code: its colours and the measures of its box, as it is drawn on the page
    readonly property var code: canvas.textEditCode
    readonly property bool isCode: code.language !== undefined
    readonly property int lineCount: edit.text.split("\n").length
    readonly property real numbersWidth: isCode && code.numbers
                                         ? String(lineCount).length * code.digitWidth + code.gap : 0
    readonly property rect codeBox: isCode ? Qt.rect(-code.padding - numbersWidth, -code.padding,
                                                     edit.width + 2 * code.padding + numbersWidth,
                                                     edit.height + 2 * code.padding)
                                           : Qt.rect(0, 0, edit.width, edit.height)

    /// Replaces a part of the text as the canvas says (code: indenting)
    function apply(change) {
        edit.remove(change.from, change.to)
        edit.insert(change.from, change.replacement)
        edit.select(change.selectionStart, change.selectionEnd)
    }

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
                // The lines keep the distance they will have on the page, also after a change of the font;
                // code is highlighted as it is written
                editor.canvas.prepareTextDocument(edit.textDocument)
                editor.canvas.prepareTextDocument(numbers.textDocument, false)
            }
            editor.wasEditing = editor.canvas.textEditing
        }
    }

    function newLine(event) {
        if (!isCode) {
            event.accepted = false
            return
        }
        edit.remove(edit.selectionStart, edit.selectionEnd)
        edit.insert(edit.cursorPosition, canvas.codeNewLine(edit.text, edit.cursorPosition))
    }

    // Under the choice of the language: some styles draw a combo box without a background of its own (Material in
    // the dark mode: light text and an outline), which on white paper could not be seen
    Rectangle {
        visible: languageBox.visible
        x: languageBox.x
        y: languageBox.y
        width: languageBox.width
        height: languageBox.height
        radius: 4
        color: languageBox.palette.window
        border.color: languageBox.palette.mid
    }

    // Code: the choice of its language, at the top right corner of its box, upright and as large as the controls
    FitComboBox {
        id: languageBox

        readonly property var languages: editor.canvas.codeLanguages
        // With the matrix itself: the transform of the editor may not have it yet when this is evaluated
        readonly property point corner: {
            const p = editor.canvas.textEditMatrix.times(
                        Qt.vector4d(editor.codeBox.x + editor.codeBox.width, editor.codeBox.y, 0, 1))
            return Qt.point(p.x, p.y)
        }

        visible: editor.isCode
        // Kept in the view: on a phone the box of the code is often narrower than this, or at the edge of the
        // screen. Below the box where there is no room above it
        x: Math.max(4, Math.min(corner.x - width, editor.width - width - 4))
        y: corner.y - height - 4 >= 4 ? corner.y - height - 4 : Math.min(boxBottom + 4, editor.height - height - 4)
        width: 140

        readonly property real boxBottom: {
            const p = editor.canvas.textEditMatrix.times(
                        Qt.vector4d(editor.codeBox.x, editor.codeBox.y + editor.codeBox.height, 0, 1))
            return p.y
        }
        focusPolicy: Qt.NoFocus
        model: [qsTr("No code")].concat(languages.map(language => language.name))
        currentIndex: languages.findIndex(language => language.id === editor.canvas.textCode) + 1
        onActivated: index => {
            editor.canvas.textCode = index === 0 ? "" : languages[index - 1].id
            edit.forceActiveFocus()
        }

        ToolTip.visible: hovered
        ToolTip.delay: 600
        ToolTip.text: qsTr("Language of the code")
    }
    Binding {
        target: editor.canvas
        property: "textEditControls"
        value: languageBox.visible && editor.visible
               ? Qt.rect(languageBox.x, languageBox.y, languageBox.width, languageBox.height) : Qt.rect(0, 0, 0, 0)
    }

    TextEdit {
        id: edit

        width: editor.canvas.textEditWrap >= 0 ? editor.canvas.textEditWrap : Math.max(implicitWidth, 40)
        transform: Matrix4x4 { matrix: editor.canvas.textEditMatrix }
        font: editor.canvas.textEditFont
        color: editor.isCode ? editor.code.text : editor.canvas.textEditColor
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
        // Spaces instead of a tab, if the settings say so (Xournal++: numberOfSpacesForTab). In code a selection
        // over several lines is indented as a whole
        Keys.onTabPressed: event => {
            const selected = edit.getText(edit.selectionStart, edit.selectionEnd)
            if (editor.isCode && selected.includes("\n")) {
                editor.apply(editor.canvas.codeIndent(edit.text, edit.selectionStart, edit.selectionEnd, false))
            } else if (editor.canvas.input.tabSpaces > 0) {
                edit.remove(edit.selectionStart, edit.selectionEnd)
                edit.insert(edit.cursorPosition, " ".repeat(editor.canvas.input.tabSpaces))
            } else {
                event.accepted = false
            }
        }
        // Code: Shift+Tab indents the lines less
        Keys.onBacktabPressed: event => {
            if (editor.isCode) {
                editor.apply(editor.canvas.codeIndent(edit.text, edit.selectionStart, edit.selectionEnd, true))
            } else {
                event.accepted = false
            }
        }
        // Code: a new line is indented as the one it comes from
        Keys.onReturnPressed: event => editor.newLine(event)
        Keys.onEnterPressed: event => editor.newLine(event)

        // Code: the box, behind the text, and the numbers of the lines
        Rectangle {
            z: -1
            visible: editor.isCode && editor.code.box
            x: editor.codeBox.x
            y: editor.codeBox.y
            width: editor.codeBox.width
            height: editor.codeBox.height
            radius: editor.isCode ? editor.code.radius : 0
            color: editor.isCode ? editor.code.background : "transparent"
        }
        TextEdit {
            id: numbers

            visible: editor.isCode && editor.code.numbers
            x: -editor.numbersWidth
            width: editor.numbersWidth - (editor.isCode ? editor.code.gap : 0)
            readOnly: true
            activeFocusOnPress: false
            font: edit.font
            color: editor.isCode ? editor.code.lineNumbers : "transparent"
            textFormat: TextEdit.PlainText
            horizontalAlignment: TextEdit.AlignRight
            text: Array.from({ length: editor.lineCount }, (_, i) => i + 1).join("\n")

            // The distance of the lines of the code; setting it changes the document, which it must not repeat
            property bool preparing: false
            onTextChanged: {
                if (!preparing) {
                    preparing = true
                    editor.canvas.prepareTextDocument(numbers.textDocument, false)
                    preparing = false
                }
            }
        }

        Rectangle {
            x: editor.codeBox.x - 4 * editor.unit
            y: editor.codeBox.y - 4 * editor.unit
            width: editor.codeBox.width + 8 * editor.unit
            height: editor.codeBox.height + 8 * editor.unit
            color: "transparent"
            border.width: editor.unit
            border.color: "#808080"
        }

        // Dragging the right border sets the width at which the lines wrap; code is not wrapped
        Rectangle {
            id: wrapHandle

            visible: !editor.isCode
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
