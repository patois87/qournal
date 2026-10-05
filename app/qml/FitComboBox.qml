import QtQuick
import QtQuick.Controls

// A combo box whose list keeps out of the room at the edges of the window that is not for the application: the
// status bar and the navigation bar of a phone (Qt 6.9 and newer). A long list reached below the status bar, where
// its first entries could not be tapped
ComboBox {
    id: box

    function safeMargin(side) {
        try {
            return Overlay.overlay.SafeArea.margins[side]
        } catch (e) {
            return 0
        }
    }

    Binding {
        target: box.popup
        property: "topMargin"
        value: box.safeMargin("top")
    }
    Binding {
        target: box.popup
        property: "bottomMargin"
        value: box.safeMargin("bottom")
    }
    Binding {
        target: box.popup
        property: "leftMargin"
        value: box.safeMargin("left")
    }
    Binding {
        target: box.popup
        property: "rightMargin"
        value: box.safeMargin("right")
    }
}
