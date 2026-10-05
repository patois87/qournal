pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qournal

// Pressure curve and stroke stabilizer. Changes apply at once, to the next stroke
Dialog {
    id: dialog

    required property PageCanvas canvas
    readonly property InputSettings input: canvas.input

    anchors.centerIn: parent
    width: Math.min(parent.width - 32, 520 * Math.max(1, font.pixelSize / 13))
    modal: true
    Overlay.modal: ModalDim {}
    title: qsTr("Pen input")
    standardButtons: Dialog.Close

    // A slider with its value next to it
    component Setting: RowLayout {
        id: setting

        property alias from: slider.from
        property alias to: slider.to
        property alias stepSize: slider.stepSize
        property real value
        property int decimals: 2

        signal moved(real value)

        Slider {
            id: slider

            Layout.fillWidth: true
            value: setting.value
            onMoved: setting.moved(value)
        }
        Label {
            Layout.preferredWidth: 44
            horizontalAlignment: Text.AlignRight
            text: slider.value.toFixed(setting.decimals)
        }
    }

    GridLayout {
        width: parent.width
        columns: 2
        columnSpacing: 12

        Label {
            Layout.columnSpan: 2
            font.bold: true
            text: qsTr("Pressure")
        }
        CheckBox {
            Layout.columnSpan: 2
            text: qsTr("The pen draws wider with more pressure")
            checked: dialog.canvas.usePressure
            onToggled: dialog.canvas.usePressure = checked
        }
        Label { text: qsTr("Minimum pressure") }
        Setting {
            from: 0.01
            to: 1
            value: dialog.input.minimumPressure
            onMoved: value => dialog.input.minimumPressure = value
        }
        Label { text: qsTr("Multiplier") }
        Setting {
            from: 0.1
            to: 5
            value: dialog.input.pressureMultiplier
            onMoved: value => dialog.input.pressureMultiplier = value
        }

        Label {
            Layout.columnSpan: 2
            Layout.topMargin: 8
            font.bold: true
            text: qsTr("Stroke stabilizer")
        }
        Label { text: qsTr("Averaging") }
        ComboBox {
            id: averagingBox

            Layout.fillWidth: true
            model: [qsTr("None"), qsTr("Arithmetic mean"), qsTr("Velocity based Gaussian weights")]
            currentIndex: dialog.input.stabilizerAveraging
            onActivated: index => dialog.input.stabilizerAveraging = index
        }
        Label {
            visible: averagingBox.currentIndex === InputSettings.Arithmetic
            text: qsTr("Number of events")
        }
        Setting {
            visible: averagingBox.currentIndex === InputSettings.Arithmetic
            from: 1
            to: 100
            stepSize: 1
            decimals: 0
            value: dialog.input.stabilizerBufferSize
            onMoved: value => dialog.input.stabilizerBufferSize = value
        }
        Label {
            visible: averagingBox.currentIndex === InputSettings.VelocityGaussian
            text: qsTr("Sigma")
        }
        Setting {
            visible: averagingBox.currentIndex === InputSettings.VelocityGaussian
            from: 0.05
            to: 5
            value: dialog.input.stabilizerSigma
            onMoved: value => dialog.input.stabilizerSigma = value
        }

        Label { text: qsTr("Preprocessor") }
        ComboBox {
            id: preprocessorBox

            Layout.fillWidth: true
            model: [qsTr("None"), qsTr("Deadzone"), qsTr("Inertia")]
            currentIndex: dialog.input.stabilizerPreprocessor
            onActivated: index => dialog.input.stabilizerPreprocessor = index
        }
        Label {
            visible: preprocessorBox.currentIndex === InputSettings.Deadzone
            text: qsTr("Radius (pixels)")
        }
        Setting {
            visible: preprocessorBox.currentIndex === InputSettings.Deadzone
            from: 0.1
            to: 30
            decimals: 1
            value: dialog.input.stabilizerDeadzoneRadius
            onMoved: value => dialog.input.stabilizerDeadzoneRadius = value
        }
        CheckBox {
            Layout.columnSpan: 2
            visible: preprocessorBox.currentIndex === InputSettings.Deadzone
            text: qsTr("Keep sharp turns sharp")
            checked: dialog.input.stabilizerCuspDetection
            onToggled: dialog.input.stabilizerCuspDetection = checked
        }
        Label {
            visible: preprocessorBox.currentIndex === InputSettings.Inertia
            text: qsTr("Drag")
        }
        Setting {
            visible: preprocessorBox.currentIndex === InputSettings.Inertia
            from: 0.05
            to: 1
            value: dialog.input.stabilizerDrag
            onMoved: value => dialog.input.stabilizerDrag = value
        }
        Label {
            visible: preprocessorBox.currentIndex === InputSettings.Inertia
            text: qsTr("Mass")
        }
        Setting {
            visible: preprocessorBox.currentIndex === InputSettings.Inertia
            from: 1
            to: 30
            decimals: 1
            value: dialog.input.stabilizerMass
            onMoved: value => dialog.input.stabilizerMass = value
        }
        CheckBox {
            Layout.columnSpan: 2
            visible: averagingBox.currentIndex !== InputSettings.NoAveraging
                     || preprocessorBox.currentIndex !== InputSettings.NoPreprocessor
            text: qsTr("Draw up to the pen when the stroke ends")
            checked: dialog.input.stabilizerFinalizeStroke
            onToggled: dialog.input.stabilizerFinalizeStroke = checked
        }
    }
}
