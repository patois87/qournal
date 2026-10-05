// A check box whose text wraps: a long text makes a form wider than a narrow window otherwise, and what is on
// the right is cut off
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

CheckBox {
    id: box

    Layout.fillWidth: true
    contentItem: Label {
        leftPadding: box.indicator.width + box.spacing
        text: box.text
        wrapMode: Text.Wrap
        verticalAlignment: Text.AlignVCenter
    }
}
