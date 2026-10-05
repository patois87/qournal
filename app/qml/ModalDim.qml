import QtQuick

// What is behind a dialog while it is open: dimmed, but not on electronic paper, where the gray of it is dithered
// and stays as a trace. The palette for electronic paper is the one whose lines are black (see Theme)
Rectangle {
    color: Qt.colorEqual(palette.mid, "black") ? "transparent" : "#50000000"
}
