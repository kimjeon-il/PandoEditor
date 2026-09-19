import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: chooser
    objectName: "objectChooser"
    required property Item mapView
    parent: Overlay.overlay
    width: Math.min(320, Math.max(1, parent ? parent.width-16 : 304))
    contentHeight: 44 + Math.min(240, candidates.contentHeight)
    height: Math.min(contentHeight + topPadding + bottomPadding,
                     Math.max(1, parent ? parent.height-16 : 300))
    property point anchorPoint: Qt.point(8,8)
    x: editor.mobileMode ? 8 : Math.max(8, Math.min(anchorPoint.x+12, parent ? parent.width-width-8 : 8))
    y: editor.mobileMode ? Math.max(8, parent ? parent.height-height-8 : 8)
                        : Math.max(8, Math.min(anchorPoint.y+12, parent ? parent.height-height-8 : 8))
    padding: 8
    modal: true
    dim: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    function openAt(point) {
        anchorPoint = mapView.mapToItem(parent, point)
        open()
        candidates.currentIndex = 0
        candidates.forceActiveFocus()
    }
    onClosed: editor.closeObjectChooser()
    contentItem: ColumnLayout {
        spacing: 4
        RowLayout {
            Layout.fillWidth: true
            Label { text: "겹친 객체 선택"; font.bold: true; Layout.fillWidth: true }
            ToolButton { objectName: "closeObjectChooser"; text: "닫기"; onClicked: chooser.close() }
        }
        ListView {
            id: candidates
            objectName: "objectChooserList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: Math.min(240, contentHeight)
            Layout.minimumHeight: 0
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: editor.objectChooserCandidates
            ScrollBar.vertical: ScrollBar {}
            Keys.onReturnPressed: function(event) { editor.chooseMapCandidate(currentIndex, !!(event.modifiers & (Qt.ControlModifier | Qt.MetaModifier))) }
            Keys.onSpacePressed: function(event) { editor.chooseMapCandidate(currentIndex, !!(event.modifiers & (Qt.ControlModifier | Qt.MetaModifier))) }
            delegate: ObjectSelectionButton {
                required property var modelData
                required property int index
                objectName: "chooserSelect_" + modelData.id
                width: candidates.width
                height: 52
                text: modelData.name + " · " + modelData.typeLabel
                highlighted: ListView.isCurrentItem || editor.selectionItems.some(function(ref) { return ref.key === modelData.key })
                Accessible.name: text
                onInvoked: function(modifiers) { editor.chooseMapCandidate(index, !!(modifiers & (Qt.ControlModifier | Qt.MetaModifier))) }
            }
        }
    }
}
