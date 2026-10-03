import QtQuick
import QtQuick.Controls
Item {
    id:handle
    property int level:1
    property var snapHeights:[]
    property real panelHeight:0
    property bool dragging:false
    signal heightChangedByUser(int level)
    signal panelHeightRequested(real height)
    implicitHeight:28;activeFocusOnTab:true
    Accessible.role:Accessible.Button
    Accessible.name:"패널 높이 · "+["접힘","중간 높이","확장"][level]
    function setLevel(value){level=Math.max(0,Math.min(2,value));heightChangedByUser(level)}
    Keys.onUpPressed:setLevel(level+1)
    Keys.onDownPressed:setLevel(level-1)
    Keys.onPressed:event=>{if(event.key===Qt.Key_Home){setLevel(0);event.accepted=true}else if(event.key===Qt.Key_End){setLevel(2);event.accepted=true}else if(event.key===Qt.Key_PageUp){setLevel(level+1);event.accepted=true}else if(event.key===Qt.Key_PageDown){setLevel(level-1);event.accepted=true}}
    Keys.onSpacePressed:setLevel((level+1)%3)
    Keys.onReturnPressed:setLevel((level+1)%3)
    Rectangle {anchors.centerIn:parent;width:40;height:4;radius:2;color:handle.activeFocus?"#316fd3":"#87939f"}
    MouseArea {
        anchors.fill:parent;preventStealing:true
        property real pressedY:0
        property real startHeight:0
        property real requestedHeight:0
        onPressed:mouse=>{pressedY=handle.mapToItem(null,mouse.x,mouse.y).y;startHeight=handle.panelHeight;requestedHeight=startHeight;handle.dragging=false;handle.forceActiveFocus()}
        onPositionChanged:mouse=>{
            if(!pressed)return
            const delta=pressedY-handle.mapToItem(null,mouse.x,mouse.y).y
            if(Math.abs(delta)>4)handle.dragging=true
            if(handle.dragging&&handle.snapHeights.length===3){requestedHeight=Math.max(handle.snapHeights[0],Math.min(handle.snapHeights[2],startHeight+delta));handle.panelHeightRequested(requestedHeight)}
        }
        onReleased:mouse=>{
            const delta=pressedY-handle.mapToItem(null,mouse.x,mouse.y).y
            if(handle.dragging&&handle.snapHeights.length===3){let index=0;for(let i=1;i<3;i++)if(Math.abs(requestedHeight-handle.snapHeights[i])<Math.abs(requestedHeight-handle.snapHeights[index]))index=i;handle.setLevel(index)}
            else if(Math.abs(delta)>18)handle.setLevel(handle.level+(delta>0?1:-1))
            else handle.setLevel((handle.level+1)%3)
            handle.dragging=false;handle.panelHeightRequested(-1)
        }
        onCanceled:{handle.dragging=false;handle.panelHeightRequested(-1)}
    }
}
