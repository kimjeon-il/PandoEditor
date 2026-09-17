import QtQuick
import QtQuick.Controls
import QtQuick.Shapes

Rectangle {
    id: view
    color: "#e8eff4"
    clip: true
    property real zoom: 1
    property real panX: 0
    property real panY: 0
    property real fitScale: Math.max(0.01, Math.min(Math.max(1,width-48)/editor.mapWidth, Math.max(1,height-48)/editor.mapHeight))
    property real mapScale: fitScale * zoom
    property real originX: (width-editor.mapWidth*mapScale)/2+panX
    property real originY: (height-editor.mapHeight*mapScale)/2+panY
    property string selectionPath: {
        for (let p of editor.paths) if (p.countryId === editor.selectedId) return p.path
        return ""
    }
    function fit() { zoom=1; panX=0; panY=0 }
    function zoomAt(factor, px, py) {
        let newZoom=Math.max(0.5,Math.min(20,zoom*factor))
        let ratio=newZoom/zoom
        panX=(px-width/2)*(1-ratio)+panX*ratio
        panY=(py-height/2)*(1-ratio)+panY*ratio
        zoom=newZoom
    }
    Connections { target: editor; function onGeometryChanged() { view.fit() } }
    Repeater {
        model: editor.layers
        delegate: Item {
            id: layerGroup
            required property var modelData
            anchors.fill: parent
            z: modelData.order
            visible: editor.layerVisuals[modelData.id] ? editor.layerVisuals[modelData.id].visible : false
            opacity: editor.layerVisuals[modelData.id] ? editor.layerVisuals[modelData.id].opacity : 1
            // Composite a full viewport once, rather than multiplying every child.
            layer.enabled: opacity > 0 && opacity < 1
            Repeater {
                model: layerGroup.modelData.paths
                delegate: Item {
                    id: countryGroup
                    required property var modelData
                    anchors.fill: parent
                    opacity: editor.countryVisuals[modelData.countryId] ? editor.countryVisuals[modelData.countryId].opacity : 1
                    layer.enabled: opacity > 0 && opacity < 1
                    Shape {
                        x: view.originX; y: view.originY
                        width: editor.mapWidth; height: editor.mapHeight
                        transform: Scale { xScale: view.mapScale; yScale: view.mapScale }
                        ShapePath {
                            strokeColor: "#61778a"
                            strokeWidth: 1/view.mapScale
                            fillColor: editor.colors[countryGroup.modelData.countryId] || "#cccccc"
                            fillRule: ShapePath.OddEvenFill
                            joinStyle: ShapePath.RoundJoin
                            PathSvg { path: countryGroup.modelData.path }
                        }
                    }
                }
            }
        }
    }
    Shape {
        z: editor.layers.length+1
        x: view.originX; y: view.originY
        width: editor.mapWidth; height: editor.mapHeight
        visible: view.selectionPath !== "" && editor.selectedEditable &&
                 !!editor.layerVisuals[editor.countryLayerId] && editor.layerVisuals[editor.countryLayerId].visible
        transform: Scale { xScale: view.mapScale; yScale: view.mapScale }
        ShapePath {
            strokeColor: "#163e64"; strokeWidth: 3/view.mapScale
            fillColor: "transparent"; fillRule: ShapePath.OddEvenFill; joinStyle: ShapePath.RoundJoin
            PathSvg { path: view.selectionPath }
        }
    }
    TapHandler {
        acceptedButtons: Qt.LeftButton
        onTapped: function(eventPoint) { editor.selectAt((eventPoint.position.x-view.originX)/view.mapScale,(eventPoint.position.y-view.originY)/view.mapScale) }
    }
    DragHandler {
        target: null
        maximumPointCount: 1
        property real startX: 0
        property real startY: 0
        onActiveChanged: if (active) { startX=view.panX; startY=view.panY }
        onActiveTranslationChanged: if (active) { view.panX=startX+activeTranslation.x; view.panY=startY+activeTranslation.y }
    }
    PinchHandler {
        target: null
        property real previousScale: 1
        onActiveChanged: previousScale=1
        onActiveScaleChanged: if (active) { view.zoomAt(activeScale/previousScale,centroid.position.x,centroid.position.y); previousScale=activeScale }
    }
    WheelHandler {
        target: null
        onWheel: function(event) { view.zoomAt(Math.pow(1.0015,event.angleDelta.y),event.x,event.y) }
    }
    Row {
        z: editor.layers.length+2
        anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12; spacing: 4
        Button { text: "+"; width: 44; Accessible.name: "확대"; onClicked: view.zoomAt(1.25,view.width/2,view.height/2) }
        Button { text: "-"; width: 44; Accessible.name: "축소"; onClicked: view.zoomAt(0.8,view.width/2,view.height/2) }
        Button { text: "전체"; onClicked: view.fit() }
    }
    Label {
        z: editor.layers.length+2
        anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 12
        text: "Made with Natural Earth · 5개국 예제"; color: "#526377"; font.pixelSize: 11
    }
}
