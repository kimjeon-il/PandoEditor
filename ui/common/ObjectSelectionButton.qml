import QtQuick
import QtQuick.Controls

// One activation path for mouse, touch, keyboard and accessibility.
ItemDelegate {
    id: control
    signal invoked(int modifiers)
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    contentItem: Label {
        text:control.text; font:control.font; textFormat:Text.PlainText
        color:control.highlighted?control.palette.highlightedText:control.palette.text
        elide:Text.ElideRight;verticalAlignment:Text.AlignVCenter
    }
    TapHandler {
        onTapped: control.invoked(point.modifiers)
    }
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            control.invoked(event.modifiers)
            event.accepted = true
        }
    }
    Accessible.onPressAction: control.invoked(Qt.NoModifier)
}
