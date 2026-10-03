import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: search
    property bool collapsed:false
    signal singleSelected()
    signal selectionStarted()
    signal selectionFinished()
    signal navigationStarted()
    spacing: 8
    property string projectInstance: editor.projectInstanceId
    onProjectInstanceChanged: {
        debounce.stop()
        field.text = editor.searchQuery
    }
    onVisibleChanged: if (!visible) editor.setHoverObject({}, "search")
    RowLayout {
        Layout.fillWidth: true
        Layout.margins: 8
        UiIcon {name:"search"}
        UiTextField {
            id: field
            objectName: "objectSearchField"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: editor.searchQuery
            placeholderText: "이름 · 유형 · ID 검색"
            Accessible.name: "지도 객체 검색"
            selectByMouse: true
            onTextEdited: debounce.restart()
            Keys.onDownPressed: { results.currentIndex=0; results.forceActiveFocus() }
        }
        UiButton {
            objectName: "searchClear"
            symbol: "close"; ToolTip.text:"검색 지우기"; Accessible.name:ToolTip.text
            focusPolicy: Qt.NoFocus
            visible: field.text.length > 0
            onClicked: { debounce.stop(); field.clear(); editor.searchQuery="";field.forceActiveFocus() }
        }
    }
    Timer {
        id: debounce
        interval: 120
        onTriggered: editor.searchQuery = field.text
    }
    Connections {
        target: editor
        function onSearchChanged() {
            if (!debounce.running && field.text !== editor.searchQuery)
                field.text = editor.searchQuery
        }
    }
    Label {
        Layout.fillWidth: true
        Layout.margins: 12
        visible: !search.collapsed && results.count === 0
        text: editor.searchQuery.trim() ? "검색 결과가 없습니다." : "객체의 이름이나 유형을 입력하세요."
        wrapMode: Text.WordWrap
    }
    ListView {
        id: results;visible:!search.collapsed
        objectName: "objectSearchResults"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: editor.searchResults
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        keyNavigationEnabled: true
        Keys.onReturnPressed: function(event) { if (currentItem) currentItem.selectWith(event.modifiers) }
        Keys.onSpacePressed: function(event) { if (currentItem) currentItem.selectWith(event.modifiers) }
        delegate: RowLayout {
            id: row
            required property var modelData
            required property int index
            width: results.width
            height: 48
            spacing: 2
            readonly property bool selected: editor.selectionItems.some(function(ref) { return ref.key === row.modelData.key })
            function selectWith(modifiers) {
                search.navigationStarted()
                const additive = !!(modifiers & (Qt.ControlModifier | Qt.MetaModifier))
                const range = !!(modifiers & Qt.ShiftModifier)
                // Range endpoints follow the currently displayed result order.
                search.selectionStarted()
                const success = editor.selectObject(modelData, range ? "range" : additive ? "toggle" : "replace", "layer-list", editor.searchResults, additive)
                search.selectionFinished()
                if (success && !additive && !range) search.singleSelected()
            }
            Item {
                Layout.preferredWidth:36;Layout.preferredHeight:30
                Image {anchors.fill:parent;source:row.modelData.flagSource||"";sourceSize:Qt.size(48*Screen.devicePixelRatio,32*Screen.devicePixelRatio);fillMode:Image.PreserveAspectFit}
                UiIcon {anchors.centerIn:parent;visible:!row.modelData.flagSource;name:row.modelData.domain==="territorial"?row.modelData.type:row.modelData.domain==="label"?"place":row.modelData.domain==="hydro"||row.modelData.domain==="hydroBuiltin"?"river":row.modelData.domain.indexOf("distribution")===0?"distribution":"type"}
            }
            ObjectSelectionButton {
                id: selectButton
                objectName: "searchSelect_" + row.modelData.id
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.fillHeight: true
                text: row.modelData.name
                highlighted: row.selected
                font.bold: editor.primaryObject.key === row.modelData.key
                Accessible.name: row.modelData.name + " · " + row.modelData.typeLabel
                Accessible.description: row.modelData.id + (row.modelData.visible ? "" : " · 숨김") + (row.modelData.locked ? " · 잠김" : "")
                onInvoked: function(modifiers) { row.selectWith(modifiers) }
                onHoveredChanged: {
                    if (hovered) editor.setHoverObject(row.modelData, "search")
                    else editor.setHoverObject({}, "search", row.modelData.key)
                }
                Component.onDestruction: editor.setHoverObject({}, "search", row.modelData.key)
            }
            UiButton {
                objectName: "searchFocus_" + row.modelData.id
                symbol:"focus";ToolTip.text:"선택 객체로 이동"
                focusPolicy: Qt.NoFocus
                Layout.preferredWidth: 40
                Layout.fillHeight: true
                Accessible.name: row.modelData.name + " 선택 객체로 이동"
                onClicked: { search.navigationStarted(); editor.focusObject(row.modelData) }
            }
        }
    }
}
