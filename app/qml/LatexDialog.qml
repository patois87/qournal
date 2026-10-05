pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qournal

// Writes or changes a LaTeX formula. LaTeX turns it into a PDF, which is put on the page
Dialog {
    id: dialog

    required property PageCanvas canvas
    property bool existing: false
    property string message: ""
    /// The command that runs LaTeX, see LatexRunner
    property alias command: runner.command
    property alias templateFile: runner.templateFile
    // The formula the preview shows, and whether the formula is put on the page when LaTeX is done
    property string previewSource: ""
    property bool applying: false

    function openFor(source, isExisting) {
        existing = isExisting
        message = ""
        applying = false
        previewSource = ""
        preview.pdf = ""
        sourceArea.text = source
        open()
        sourceArea.forceActiveFocus()
        sourceArea.selectAll()
    }

    anchors.centerIn: parent
    width: Math.min(parent.width - 32, 560 * Math.max(1, font.pixelSize / 13))
    modal: true
    Overlay.modal: ModalDim {}
    title: existing ? qsTr("Edit formula") : qsTr("New formula")

    onClosed: {
        previewTimer.stop()
        runner.stop()
    }

    LatexRunner {
        id: runner

        onFinished: pdf => {
            preview.pdf = pdf
            dialog.previewSource = previewTimer.requested
            dialog.message = ""
            if (dialog.applying) {
                dialog.canvas.applyLatex(dialog.previewSource, pdf)
                dialog.close()
            }
        }
        onFailed: message => {
            dialog.applying = false
            dialog.message = message
        }
    }

    // The preview follows the formula, a moment after the last key
    Timer {
        id: previewTimer

        property string requested: ""

        function start_() {
            requested = sourceArea.text
            runner.run(requested, dialog.canvas.color)
        }

        interval: 700
        onTriggered: start_()
    }

    ColumnLayout {
        width: parent.width
        spacing: 8

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            visible: !runner.available
            text: qsTr("LaTeX was not found on this device. Formulas can be shown, but not created or changed. "
                       + "The command is: %1").arg(runner.command)
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            visible: runner.builtin
            text: qsTr("LaTeX was not found on this device: the application sets the formula itself. It knows the "
                       + "formulas of LaTeX, but no packages and no template.")
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 120

            TextArea {
                id: sourceArea

                font.family: "monospace"
                wrapMode: TextArea.Wrap
                placeholderText: qsTr("The formula, in LaTeX math mode")
                onTextChanged: {
                    if (dialog.visible && runner.available && text.trim() !== "") {
                        previewTimer.restart()
                    }
                }
            }
        }
        FormulaPreview {
            id: preview

            Layout.fillWidth: true
            Layout.preferredHeight: 110
            visible: runner.available
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 140
            visible: dialog.message !== ""

            TextArea {
                readOnly: true
                font.family: "monospace"
                wrapMode: TextArea.Wrap
                text: dialog.message
            }
        }
        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            visible: runner.running
            running: runner.running
        }
    }

    footer: DialogButtonBox {
        Button {
            text: dialog.existing ? qsTr("Apply") : qsTr("Insert")
            enabled: runner.available && !dialog.applying && sourceArea.text.trim() !== ""
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: {
                if (dialog.previewSource === sourceArea.text && preview.pdf.byteLength > 0 && !runner.running) {
                    // The preview is this formula already
                    dialog.canvas.applyLatex(sourceArea.text, preview.pdf)
                    dialog.close()
                } else {
                    dialog.applying = true
                    previewTimer.stop()
                    previewTimer.start_()
                }
            }
        }
        Button {
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }
}
