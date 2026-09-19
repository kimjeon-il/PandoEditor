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
    signal selectionNavigationStarted()
    signal selectionPointerChanged(bool down)
    property real controlsTopMargin:12
    property point chooserPoint: Qt.point(0,0)
    property string mapHoverKey: ""
    readonly property var selectedPaths: {
        const keys = editor.selectionItems.map(function(ref) { return ref.id })
        return editor.paths.filter(function(path) {
            return keys.indexOf(path.countryId)>=0 && editor.countryVisuals[path.countryId]
                && editor.countryVisuals[path.countryId].visible
        })
    }
    function dismissPopup() {
        if (objectChooser.visible) { objectChooser.close(); return true }
        return false
    }
    function focusRect(left, top, objectWidth, objectHeight, maxZoom) {
        if (![left,top,objectWidth,objectHeight,maxZoom].every(Number.isFinite)) return
        editor.closeObjectChooser()
        // Same fit factors and caps as web focusCountry, on the existing Qt projection.
        const targetWidth = Math.max(96,width-48)*0.82
        const targetHeight = Math.max(96,height-48)*0.82
        const fitted = Math.min(targetWidth/Math.max(1,objectWidth*fitScale),
                                targetHeight/Math.max(1,objectHeight*fitScale))*0.88
        zoom = Math.max(1.25,Math.min(maxZoom,fitted))
        panX = (editor.mapWidth/2-left-objectWidth/2)*mapScale
        panY = (editor.mapHeight/2-top-objectHeight/2)*mapScale
    }
    function invalidatePick() { editor.closeObjectChooser() }
    onZoomChanged: invalidatePick()
    onPanXChanged: invalidatePick()
    onPanYChanged: invalidatePick()
    onWidthChanged: invalidatePick()
    onHeightChanged: invalidatePick()
    function fit() { zoom=1; panX=0; panY=0 }
    function zoomAt(factor, px, py) {
        let newZoom=Math.max(0.5,Math.min(20,zoom*factor))
        let ratio=newZoom/zoom
        panX=(px-width/2)*(1-ratio)+panX*ratio
        panY=(py-height/2)*(1-ratio)+panY*ratio
        zoom=newZoom
    }
    Connections {
        target: editor
        function onGeometryChanged() { view.fit() }
        function onFocusRequested(left,top,width,height,maxZoom) { view.focusRect(left,top,width,height,maxZoom) }
        function onObjectChooserChanged() {
            if (editor.objectChooserOpen) objectChooser.openAt(view.chooserPoint)
            else objectChooser.close()
        }
    }
    ObjectChooser { id: objectChooser; mapView: view }

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
                    visible: !!editor.countryVisuals[modelData.countryId] && editor.countryVisuals[modelData.countryId].visible
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
    Repeater {
        model: view.selectedPaths
        delegate: Shape {
            id: outline
            required property var modelData
            objectName: "selectionOutline_" + modelData.countryId
            z: editor.layers.length+1
            x: view.originX; y: view.originY
            width: editor.mapWidth; height: editor.mapHeight
            transform: Scale { xScale: view.mapScale; yScale: view.mapScale }
            ShapePath {
                strokeColor: "#163e64"
                strokeWidth: (editor.primaryObject.id===outline.modelData.countryId ? 3 : 2)/view.mapScale
                fillColor: "transparent"; fillRule: ShapePath.OddEvenFill; joinStyle: ShapePath.RoundJoin
                PathSvg { path: outline.modelData.path }
            }
        }
    }
    TapHandler {
        id: mapTap
        enabled: !objectChooser.visible
        acceptedButtons: Qt.LeftButton
        property int gestureModifiers: Qt.NoModifier
        onPressedChanged: {
            view.selectionPointerChanged(pressed)
            if(pressed){gestureModifiers=point.modifiers;view.selectionNavigationStarted()}
        }
        onTapped: function(eventPoint) {
            view.selectionNavigationStarted()
            view.chooserPoint = eventPoint.position
            editor.beginMapSelection((eventPoint.position.x-view.originX)/view.mapScale,
                                     (eventPoint.position.y-view.originY)/view.mapScale,
                                     !!(gestureModifiers & (Qt.ControlModifier | Qt.MetaModifier)),view.mapScale)
        }
    }
    HoverHandler {
        id: mapHover
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
        function updateHover() {
            if (!hovered || editor.objectChooserOpen) return
            const ref=editor.pickObject((point.position.x-view.originX)/view.mapScale,
                                        (point.position.y-view.originY)/view.mapScale)
            editor.setHoverObject(ref,"map")
            view.mapHoverKey=ref.key || ""
        }
        onPointChanged: updateHover()
        onHoveredChanged: {
            if (hovered) updateHover()
            else editor.setHoverObject({},"map",view.mapHoverKey)
        }
    }
    DragHandler {
        enabled: !objectChooser.visible
        target: null
        maximumPointCount: 1
        property real startX: 0
        property real startY: 0
        onActiveChanged: if (active) { startX=view.panX; startY=view.panY }
        onActiveTranslationChanged: if (active) { view.panX=startX+activeTranslation.x; view.panY=startY+activeTranslation.y }
    }
    PinchHandler {
        enabled: !objectChooser.visible
        target: null
        property real previousScale: 1
        onActiveChanged: previousScale=1
        onActiveScaleChanged: if (active) { view.zoomAt(activeScale/previousScale,centroid.position.x,centroid.position.y); previousScale=activeScale }
    }
    WheelHandler {
        enabled: !objectChooser.visible
        target: null
        onWheel: function(event) { view.zoomAt(Math.pow(1.0015,event.angleDelta.y),event.x,event.y) }
    }
    Row {
        z: editor.layers.length+2
        anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12; anchors.topMargin:view.controlsTopMargin; spacing: 4
        Button { focusPolicy: Qt.NoFocus; text: "+"; width: 44; Accessible.name: "확대"; onClicked: view.zoomAt(1.25,view.width/2,view.height/2) }
        Button { focusPolicy: Qt.NoFocus; text: "-"; width: 44; Accessible.name: "축소"; onClicked: view.zoomAt(0.8,view.width/2,view.height/2) }
        Button { focusPolicy: Qt.NoFocus; text: "전체"; onClicked: view.fit() }
    }
    Label {
        z: editor.layers.length+2
        anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 12
        text: "Made with Natural Earth · 5개국 예제"; color: "#526377"; font.pixelSize: 11
    }
}
