import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
import QtQuick.Dialogs as Native
import Pandoeditor.Windowing 1.0

Rectangle {
    id: view
    color: editor.appearancePreferences.effectiveTheme === "dark" ? "#101820" : "#e8eff4"
    clip: true
    property real zoom: 1
    property real panX: 0
    property real panY: 0
    readonly property bool globeMode: editor.projectionMode === "globe"
    property real globeZoom: 1
    property real globeLongitude: 0
    property real globeLatitude: 0
    property string synchronizedProjection: ""
    property real fitScale: Math.max(0.01, Math.min(Math.max(1,width-48)/editor.mapWidth, Math.max(1,height-48)/editor.mapHeight))
    property real mapScale: fitScale * zoom
    property real originX: (width-editor.mapWidth*mapScale)/2+panX
    property real originY: (height-editor.mapHeight*mapScale)/2+panY
    signal selectionNavigationStarted()
    signal selectionPointerChanged(bool down)
    property real controlsTopMargin:12
    property point chooserPoint: Qt.point(0,0)
    property string mapHoverKey: ""
    readonly property bool geometryEditing: editor.geometryEditState.active === true
    focus: true
    Keys.onPressed: function(event) {
        if (!geometryEditing) return
        if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            if (editor.geometryDeleteSelectedVertex()) event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (editor.requestGeometryPreview()) event.accepted = true
        } else if (event.key === Qt.Key_Escape) {
            editor.cancelGeometryEdit()
            event.accepted = true
        }
    }
    readonly property var selectedPaths: {
        const keys = editor.selectionItems.map(function(ref) { return ref.domain === "territorial" ? ref.id : "content/" + ref.domain + "/" + ref.id })
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
    function publishView() {
        if (width <= 0 || height <= 0) return
        if (globeMode) {
            editor.publishMapView({"viewportWidth":width,"viewportHeight":height,
                "scale":Math.max(1,Math.min(width,height)*0.44*globeZoom),
                "translateX":width/2,"translateY":height/2,
                "centerLongitude":globeLongitude,"centerLatitude":globeLatitude})
        } else {
            const scale=mapScale*editor.hydroProjection.cosLatitude*180/Math.PI
            editor.publishMapView({"viewportWidth":width,"viewportHeight":height,"scale":scale,
                "translateX":originX-editor.hydroProjection.minX*mapScale,
                "translateY":originY+editor.hydroProjection.maxLatitude*mapScale,
                "centerLongitude":0,"centerLatitude":0})
        }
    }
    function syncProjectionCamera() {
        const state=editor.mapViewState
        synchronizedProjection=editor.projectionMode
        if (globeMode) {
            globeLongitude=state.centerLongitude || 0
            globeLatitude=state.centerLatitude || 0
            const oldFit=Math.min(state.viewportWidth || 1,state.viewportHeight || 1)*0.44
            globeZoom=oldFit>1 ? Math.max(0.5,Math.min(20,state.scale/oldFit)) : 1
        } else {
            if ((state.viewportWidth || 1) <= 1 && (state.viewportHeight || 1) <= 1) {
                zoom=1; panX=0; panY=0
            } else {
                const flatMapScale=state.scale/(Math.max(0.01,editor.hydroProjection.cosLatitude)*180/Math.PI)
                zoom=Math.max(0.5,Math.min(20,flatMapScale/fitScale))
                panX=state.translateX+editor.hydroProjection.minX*flatMapScale-(width-editor.mapWidth*flatMapScale)/2
                panY=state.translateY-editor.hydroProjection.maxLatitude*flatMapScale-(height-editor.mapHeight*flatMapScale)/2
            }
        }
        viewPublish.restart()
    }
    Timer { id: viewPublish; interval: 0; repeat: false; onTriggered: view.publishView() }
    Timer {
        id: hydroRequest
        interval: 40; repeat: false
        onTriggered: {
            editor.requestHydroViewport(view.zoom,view.mapScale,view.originX,view.originY,view.width,view.height)
            editor.requestTerrainViewport(view.mapScale,view.originX,view.originY,view.width,view.height)
        }
    }
    onZoomChanged: { invalidatePick(); hydroRequest.restart(); viewPublish.restart() }
    onPanXChanged: { invalidatePick(); hydroRequest.restart(); viewPublish.restart() }
    onPanYChanged: { invalidatePick(); hydroRequest.restart(); viewPublish.restart() }
    onGlobeZoomChanged: { invalidatePick(); hydroRequest.restart(); viewPublish.restart() }
    onGlobeLongitudeChanged: { invalidatePick(); hydroRequest.restart(); viewPublish.restart() }
    onGlobeLatitudeChanged: { invalidatePick(); hydroRequest.restart(); viewPublish.restart() }
    onWidthChanged: { invalidatePick(); hydroRequest.restart(); viewPublish.restart() }
    onHeightChanged: { invalidatePick(); hydroRequest.restart(); viewPublish.restart() }
    Component.onCompleted: syncProjectionCamera()
    function fit() { if(globeMode){globeZoom=1;globeLongitude=0;globeLatitude=0}else{zoom=1;panX=0;panY=0} }
    function zoomAt(factor, px, py) {
        if (globeMode) { globeZoom=Math.max(0.5,Math.min(20,globeZoom*factor)); return }
        let newZoom=Math.max(0.5,Math.min(20,zoom*factor))
        let ratio=newZoom/zoom
        panX=(px-width/2)*(1-ratio)+panX*ratio
        panY=(py-height/2)*(1-ratio)+panY*ratio
        zoom=newZoom
    }
    Connections {
        target: editor
        // Geometry edits retain the user's inspection position.  Explicit focus
        // and the '전체' button remain the only viewport-reset routes.
        function onGeometryChanged() { view.invalidatePick(); hydroRequest.restart() }
        function onStateChanged() { hydroRequest.restart() }
        function onViewStateChanged() {
            if (editor.projectionMode !== view.synchronizedProjection) view.syncProjectionCamera()
        }
        function onGeometryEditChanged() {
            if (editor.geometryEditState.active === true && view.globeMode) editor.setProjectionMode("flat")
        }
        function onFocusRequested(left,top,width,height,maxZoom) { view.focusRect(left,top,width,height,maxZoom) }
        function onObjectChooserChanged() {
            if (editor.objectChooserOpen) objectChooser.openAt(view.chooserPoint)
            else objectChooser.close()
        }
    }
    ObjectChooser { id: objectChooser; mapView: view }

    // Terrain and reference images share the same immutable geographic view
    // snapshot as countries and picking.
    Repeater {
        model: editor.terrainTiles
        delegate: GeographicImageItem {
            required property var modelData
            anchors.fill: parent
            sceneBridge: editor.mapSceneBridge
            source: modelData.source
            smooth: true
            west: modelData.west; east: modelData.east
            south: modelData.south; north: modelData.north
            colorMode: editor.terrainMode === "gray" ? "gray" : "color"
            z: -0.5
        }
    }

    ReferenceImageLibrary { id: referenceImages }

    Repeater {
        model: referenceImages.images
        delegate: ReferenceImageItem {
            required property var modelData
            x: view.originX + modelData.x * view.mapScale
            y: view.originY + modelData.y * view.mapScale
            width: modelData.width * view.mapScale
            height: modelData.height * view.mapScale
            source: modelData.source
            opacity: modelData.opacity
            visible: modelData.visible && !view.globeMode
            blendMode: modelData.blend
            warpMode: modelData.warpMode
            controlPoints: modelData.controlPoints
            imageRect: Qt.rect(0,0,width,height)
            flipX: modelData.flipX
            flipY: modelData.flipY
            rotation: modelData.rotation
            transformOrigin: Item.Center
            z: 0.5
            DragHandler {
                enabled: !parent.modelData.locked && !view.geometryEditing
                target: null
                property real startX: 0
                property real startY: 0
                onActiveChanged: {
                    if (active) {
                        startX=parent.modelData.x; startY=parent.modelData.y
                        referenceImages.beginGesture(parent.modelData.id)
                    } else referenceImages.commitGesture()
                }
                onActiveTranslationChanged: if(active) referenceImages.updateGesture({
                    "x":startX+activeTranslation.x/view.mapScale,
                    "y":startY+activeTranslation.y/view.mapScale
                })
            }
            PinchHandler {
                enabled: !parent.modelData.locked && !view.geometryEditing
                target: null
                property real startWidth: 0
                property real startHeight: 0
                property real startRotation: 0
                onActiveChanged: {
                    if(active) {
                        startWidth=parent.modelData.width; startHeight=parent.modelData.height
                        startRotation=parent.modelData.rotation
                        referenceImages.beginGesture(parent.modelData.id)
                    } else referenceImages.commitGesture()
                }
                onActiveScaleChanged: if(active) referenceImages.updateGesture({
                    "width":startWidth*activeScale,"height":startHeight*activeScale,
                    "rotation":startRotation+activeRotation
                })
            }
            WheelHandler {
                enabled: !parent.modelData.locked && !view.geometryEditing
                target: null
                onWheel: function(event) {
                    let factor=Math.pow(1.0015,event.angleDelta.y)
                    if(referenceImages.beginGesture(parent.modelData.id)) {
                        if(event.modifiers & Qt.ShiftModifier)
                            referenceImages.updateGesture({"rotation":parent.modelData.rotation+event.angleDelta.y/24})
                        else referenceImages.updateGesture({"width":parent.modelData.width*factor,"height":parent.modelData.height*factor})
                        referenceImages.commitGesture()
                    }
                    event.accepted=true
                }
            }
        }
    }
    Repeater {
        model: referenceImages.images
        delegate: GeographicImageItem {
            required property var modelData
            anchors.fill: parent
            sceneBridge: editor.mapSceneBridge
            source: modelData.source
            visible: modelData.visible && view.globeMode
            opacity: modelData.opacity
            west: (modelData.x + editor.hydroProjection.minX) / editor.hydroProjection.cosLatitude
            east: (modelData.x + modelData.width + editor.hydroProjection.minX) / editor.hydroProjection.cosLatitude
            north: editor.hydroProjection.maxLatitude - modelData.y
            south: editor.hydroProjection.maxLatitude - modelData.y - modelData.height
            z: 0.5
        }
    }

    GpuMapItem {
        id: gpuMapRenderer
        objectName: "gpuMapRenderer"
        anchors.fill: parent
        sceneBridge: editor.mapSceneBridge
        contentReady: !editor.hydroViewportLoaded
        uploadBudgetBytes: editor.renderQuality.uploadBudgetBytes
        onFrameSampled: function(milliseconds) { editor.recordMapFrame(milliseconds) }
        originX: view.originX; originY: view.originY; mapScale: view.mapScale
        mapCosLatitude: editor.hydroProjection.cosLatitude
        mapMinX: editor.hydroProjection.minX
        mapMaxLatitude: editor.hydroProjection.maxLatitude
        z: 0
    }
    MapRenderItem {
        id: canonicalMapRenderer
        objectName: "canonicalMapRenderer"
        anchors.fill: parent
        visible: !gpuMapRenderer.rendererReady && !gpuMapRenderer.forcedGpu
        sceneBridge: editor.mapSceneBridge
        smoothLines: editor.appearancePreferences.smoothLines !== false
        paths: editor.paths
        visuals: editor.countryVisuals
        hydroSource: editor.hydroSource
        hydroProjection: editor.hydroProjection
        hydroStyle: editor.hydroStyle
        hiddenHydroIds: editor.hiddenHydroIds
        selectedHydroId: editor.primaryObject.domain === "hydroBuiltin" ? editor.primaryObject.id : ""
        selectedPaths: view.selectedPaths
        primaryId: editor.primaryObject.domain === "territorial" ? editor.primaryObject.id : editor.primaryObject.id ? "content/" + editor.primaryObject.domain + "/" + editor.primaryObject.id : ""
        originX: view.originX; originY: view.originY; mapScale: view.mapScale
        z: 0
    }
    Label {
        objectName: "gpuRendererDiagnostic"
        visible: gpuMapRenderer.forcedGpu && !gpuMapRenderer.rendererReady
        text: gpuMapRenderer.diagnostic
        color: "#8b1e1e"
        anchors.centerIn: parent
        z: 10
    }
    Label {
        objectName: "worldDatasetStatus"
        visible: editor.worldStatus === "loading-preview" ||
                 editor.worldStatus === "preview" ||
                 editor.worldStatus === "canonical-pending-mesh" ||
                 editor.worldStatus === "unavailable" ||
                 editor.worldStatus === "canonical-mesh-unavailable"
        text: editor.worldStatus === "unavailable" ||
              editor.worldStatus === "canonical-mesh-unavailable"
              ? "고정 세계지도 자료를 사용할 수 없습니다"
              : "세계지도 자료 준비 중"
        color: "#243f53"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 10
        z: 9
    }
    Button {
        id: referenceImageButton
        objectName: "referenceImageButton"
        anchors.left: parent.left; anchors.top: parent.top
        anchors.margins: 12
        text: "참조 이미지"
        z: 20
        onClicked: referenceImageMenu.open()
    }
    Menu {
        id: referenceImageMenu
        y: referenceImageButton.y + referenceImageButton.height
        MenuItem { text: "이미지 추가…"; onTriggered: referenceImageOpen.open() }
        MenuItem { text: "배치 되돌리기"; enabled: referenceImages.canUndo; onTriggered: referenceImages.undo() }
        MenuItem { text: "배치 다시 실행"; enabled: referenceImages.canRedo; onTriggered: referenceImages.redo() }
        MenuSeparator {}
        Repeater {
            model: referenceImages.images
            delegate: Menu {
                id: imageMenu
                required property var modelData
                title: modelData.name + (modelData.locked ? "  🔒" : "")
                MenuItem { text: imageMenu.modelData.visible ? "숨기기" : "표시"; onTriggered: referenceImages.updateImage(imageMenu.modelData.id,{"visible":!imageMenu.modelData.visible}) }
                MenuItem { text: imageMenu.modelData.locked ? "잠금 해제" : "잠금"; onTriggered: referenceImages.updateImage(imageMenu.modelData.id,{"locked":!imageMenu.modelData.locked}) }
                Menu {
                    title: "혼합: " + imageMenu.modelData.blend
                    MenuItem { text:"Normal"; onTriggered:referenceImages.updateImage(imageMenu.modelData.id,{"blend":"normal"}) }
                    MenuItem { text:"Multiply"; onTriggered:referenceImages.updateImage(imageMenu.modelData.id,{"blend":"multiply"}) }
                    MenuItem { text:"Screen"; onTriggered:referenceImages.updateImage(imageMenu.modelData.id,{"blend":"screen"}) }
                    MenuItem { text:"Difference"; onTriggered:referenceImages.updateImage(imageMenu.modelData.id,{"blend":"difference"}) }
                }
                MenuItem { text:"투명도 -"; enabled:!imageMenu.modelData.locked; onTriggered:referenceImages.updateImage(imageMenu.modelData.id,{"opacity":Math.max(0,imageMenu.modelData.opacity-.1)}) }
                MenuItem { text:"투명도 +"; enabled:!imageMenu.modelData.locked; onTriggered:referenceImages.updateImage(imageMenu.modelData.id,{"opacity":Math.min(1,imageMenu.modelData.opacity+.1)}) }
                MenuItem { text:"앞으로"; enabled:!imageMenu.modelData.locked; onTriggered:referenceImages.moveImage(imageMenu.modelData.id,referenceImages.images.length-1) }
                MenuItem { text:"뒤로"; enabled:!imageMenu.modelData.locked; onTriggered:referenceImages.moveImage(imageMenu.modelData.id,0) }
                MenuItem { text:"삭제"; enabled:!imageMenu.modelData.locked; onTriggered:referenceImages.removeImage(imageMenu.modelData.id) }
            }
        }
    }
    Native.FileDialog {
        id: referenceImageOpen
        title: "참조 이미지 추가"
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp)"]
        onAccepted: referenceImages.importImage(selectedFile)
    }
    readonly property var placedLabels: {
        const revision = editor.presentationRevision
        const selection = editor.selectionRevision
        const quality = editor.renderQuality.revision
        return editor.labelLayout(mapScale,originX,originY,zoom,width,height)
    }
    Repeater {
        model: view.placedLabels
        delegate: Column {
            objectName: "mapPlacedLabel"
            required property var modelData
            x: modelData.x-width/2
            y: modelData.y-height/2
            z: editor.layers.length+2
            Image { objectName: "mapPlacedFlag"; anchors.horizontalCenter: parent.horizontalCenter; width:24; height:16; fillMode:Image.PreserveAspectFit; source:parent.modelData.flagSource||""; visible:!!parent.modelData.flagVisible&&source.toString()!=="" }
            Label { objectName: "mapPlacedText"; textFormat:Text.PlainText; anchors.horizontalCenter:parent.horizontalCenter; text:parent.modelData.name||""; visible:parent.modelData.nameVisible!==false; color:"#243746"; style:Text.Outline; styleColor:"#ffffff"; font.pixelSize:12; font.weight:Font.DemiBold }
            DragHandler {
                enabled: !view.geometryEditing
                target: null
                onActiveChanged: if(!active && activeTranslation.x*activeTranslation.x+activeTranslation.y*activeTranslation.y>4)
                    editor.setLabelMapPosition(parent.modelData.ref,
                        (parent.modelData.x+activeTranslation.x-view.originX)/view.mapScale,
                        (parent.modelData.y+activeTranslation.y-view.originY)/view.mapScale)
            }
        }
    }
    TapHandler {
        id: mapTap
        enabled: !objectChooser.visible && !view.geometryEditing
        acceptedButtons: Qt.LeftButton
        property int gestureModifiers: Qt.NoModifier
        onPressedChanged: {
            view.selectionPointerChanged(pressed)
            if(pressed){gestureModifiers=point.modifiers;view.selectionNavigationStarted()}
        }
        onTapped: function(eventPoint) {
            view.selectionNavigationStarted()
            view.chooserPoint = eventPoint.position
            editor.beginMapSelectionScreen(eventPoint.position.x,eventPoint.position.y,
                                           !!(gestureModifiers & (Qt.ControlModifier | Qt.MetaModifier)),view.globeMode?view.globeZoom:view.zoom)
        }
    }
    TapHandler {
        id: geometryTap
        enabled: view.geometryEditing && !objectChooser.visible
        acceptedButtons: Qt.LeftButton
        onTapped: function(eventPoint) {
            view.forceActiveFocus()
            const x=(eventPoint.position.x-view.originX)/view.mapScale
            const y=(eventPoint.position.y-view.originY)/view.mapScale
            if (editor.geometryEditState.tool === "draw" || editor.geometryEditState.tool === "annex" || editor.geometryEditState.tool === "split") editor.geometryAddPoint(x,y,(editor.mobileMode?18:10)/view.mapScale)
            else if (editor.geometryEditState.tool !== "move") editor.geometrySelectNearest(x,y,(editor.mobileMode?18:10)/view.mapScale)
        }
        onDoubleTapped: function(eventPoint) {
            if (editor.geometryEditState.tool !== "draw" && editor.geometryEditState.tool !== "move")
                editor.geometryInsertNearest((eventPoint.position.x-view.originX)/view.mapScale,
                                             (eventPoint.position.y-view.originY)/view.mapScale,
                                             (editor.mobileMode?18:10)/view.mapScale)
        }
    }
    HoverHandler {
        id: mapHover
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
        function updateHover() {
            if (!hovered || editor.objectChooserOpen) return
            const ref=editor.pickObjectScreen(point.position.x,point.position.y,view.globeMode?view.globeZoom:view.zoom)
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
        enabled: !objectChooser.visible && !view.geometryEditing
        target: null
        maximumPointCount: 1
        property real startX: 0
        property real startY: 0
        property real startLongitude: 0
        property real startLatitude: 0
        onActiveChanged: { if (active) { startX=view.panX; startY=view.panY; startLongitude=view.globeLongitude; startLatitude=view.globeLatitude; editor.beginMapInteraction() }
                           else editor.endMapInteraction() }
        onActiveTranslationChanged: if (active) {
            if (view.globeMode) {
                const radius=Math.max(1,Math.min(view.width,view.height)*0.44*view.globeZoom)
                view.globeLongitude=startLongitude-activeTranslation.x/radius*180/Math.PI
                view.globeLatitude=Math.max(-90,Math.min(90,startLatitude+activeTranslation.y/radius*180/Math.PI))
            } else { view.panX=startX+activeTranslation.x; view.panY=startY+activeTranslation.y }
        }
    }
    DragHandler {
        id: geometryDrag
        enabled: view.geometryEditing && editor.geometryEditState.tool !== "move" && editor.geometryEditState.selectedVertex >= 0 && !objectChooser.visible
        target: null
        maximumPointCount: 1
        onActiveChanged: {
            if (active) { editor.beginMapInteraction(); editor.geometryBeginVertexDrag() }
            else { editor.geometryEndVertexDrag(false); editor.endMapInteraction() }
        }
        onActiveTranslationChanged: if (active) {
            editor.geometryMoveSelectedVertex((centroid.position.x-view.originX)/view.mapScale,
                                              (centroid.position.y-view.originY)/view.mapScale,
                                              (editor.mobileMode?18:10)/view.mapScale)
        }
    }
    DragHandler {
        id: geometryObjectDrag
        enabled: view.geometryEditing && editor.geometryEditState.tool === "move" && !objectChooser.visible
        acceptedButtons: Qt.LeftButton
        target: null; maximumPointCount: 1
        onActiveChanged: {
            if (active) { editor.beginMapInteraction(); editor.geometryBeginObjectDrag() }
            else { editor.geometryEndObjectDrag(false); editor.endMapInteraction() }
        }
        onActiveTranslationChanged: if (active)
            editor.geometryTranslateObject(activeTranslation.x/view.mapScale,activeTranslation.y/view.mapScale)
    }
    DragHandler {
        id: geometryTwoFingerPan
        enabled: view.geometryEditing && !objectChooser.visible
        target: null; minimumPointCount: 2; maximumPointCount: 2
        property real startX: 0; property real startY: 0
        onActiveChanged: {
            if (active) { editor.beginMapInteraction(); editor.geometryEndVertexDrag(true);
                          editor.geometryEndObjectDrag(true); startX=view.panX; startY=view.panY }
            else editor.endMapInteraction()
        }
        onActiveTranslationChanged: if (active) { view.panX=startX+activeTranslation.x; view.panY=startY+activeTranslation.y }
    }
    DragHandler {
        enabled: !objectChooser.visible
        acceptedButtons: Qt.MiddleButton
        target: null; maximumPointCount: 1
        property real startX: 0; property real startY: 0
        onActiveChanged: {
            if(active){editor.beginMapInteraction();startX=view.panX;startY=view.panY}
            else editor.endMapInteraction()
        }
        onActiveTranslationChanged: if(active){view.panX=startX+activeTranslation.x;view.panY=startY+activeTranslation.y}
    }
    PinchHandler {
        enabled: !objectChooser.visible
        target: null
        property real previousScale: 1
        onActiveChanged: { previousScale=1; if(active) editor.beginMapInteraction(); else editor.endMapInteraction() }
        onActiveScaleChanged: if (active) { view.zoomAt(activeScale/previousScale,centroid.position.x,centroid.position.y); previousScale=activeScale }
    }
    Timer {
        id: wheelSettle
        interval: 180; repeat: false
        onTriggered: editor.endMapInteraction()
    }
    WheelHandler {
        enabled: !objectChooser.visible
        target: null
        onWheel: function(event) {
            if(!wheelSettle.running) editor.beginMapInteraction()
            wheelSettle.restart()
            view.zoomAt(Math.pow(1.0015,event.angleDelta.y),event.x,event.y)
        }
    }
    Repeater {
        model: editor.geometryDraftPaths
        delegate: Item {
            required property var modelData
            anchors.fill: parent
            z: editor.layers.length + 5
            Shape {
                x: view.originX; y: view.originY
                width: editor.mapWidth; height: editor.mapHeight
                transform: Scale { xScale: view.mapScale; yScale: view.mapScale }
                ShapePath {
                    strokeColor: "#d97706"; strokeWidth: 2 / view.mapScale
                    fillColor: modelData.hole ? "#00000000" : "#60f59e0b"
                    fillRule: ShapePath.OddEvenFill; joinStyle: ShapePath.RoundJoin
                    PathSvg { path: modelData.path }
                }
            }
            Repeater {
                model: modelData.vertices
                delegate: Rectangle {
                    required property var modelData
                    x: view.originX + modelData.x * view.mapScale - width / 2
                    y: view.originY + modelData.y * view.mapScale - height / 2
                    width: 10; height: 10; radius: 5
                    color: editor.geometryEditState.selectedVertex === modelData.vertex ? "#b45309" : "#ffffff"
                    border.color: "#92400e"; border.width: 2
                }
            }
        }
    }
    Rectangle {
        z: editor.layers.length + 8; width: 14; height: 14; radius: 7
        visible: view.geometryEditing && Number.isFinite(editor.geometryEditState.snapX) && Number.isFinite(editor.geometryEditState.snapY)
        x: view.originX + editor.geometryEditState.snapX * view.mapScale - width/2
        y: view.originY + editor.geometryEditState.snapY * view.mapScale - height/2
        color: "#22ffffff"; border.color: "#f59e0b"; border.width: 2
    }
    Row {
        z: editor.layers.length+2
        anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12; anchors.topMargin:view.controlsTopMargin; spacing: 4
        Button { focusPolicy: Qt.NoFocus; text: "+"; width: 44; Accessible.name: "확대"; onClicked: view.zoomAt(1.25,view.width/2,view.height/2) }
        Button { focusPolicy: Qt.NoFocus; text: "-"; width: 44; Accessible.name: "축소"; onClicked: view.zoomAt(0.8,view.width/2,view.height/2) }
        Button { focusPolicy: Qt.NoFocus; text: "전체"; onClicked: view.fit() }
    }
    Row {
        z: editor.layers.length+6
        anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 12; anchors.topMargin: view.controlsTopMargin
        spacing: 4
        visible: !view.geometryEditing && editor.selectionItems.length === 1 && editor.selectedEditable
        Button { objectName: "beginGeometryEdit"; text: "도형 편집"; onClicked: editor.beginGeometryEdit("edit") }
        Button { objectName: "beginGeometryDraw"; text: "다시 그리기"; onClicked: editor.beginGeometryDraw() }
    }
    Rectangle {
        z: editor.layers.length+7
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 12
        visible: view.geometryEditing; color: "#ffffffee"; border.color: "#d7b77f"; radius: 6; height: geometryTools.implicitHeight + 16
        Row {
            id: geometryTools; anchors.centerIn: parent; spacing: 6
            Label { visible: editor.geometryEditState.tool !== "draw" && editor.geometryEditState.tool !== "move"; text: "변을 두 번 탭해 점 추가"; verticalAlignment: Text.AlignVCenter }
            Button { objectName: "geometryMoveObject"; visible: editor.geometryEditState.active === true && editor.geometryEditState.target && editor.geometryEditState.target.domain !== "territorial" && editor.geometryEditState.tool !== "draw"; text: editor.geometryEditState.tool === "move" ? "점 편집" : "전체 이동"; onClicked: editor.geometrySetMoveMode(editor.geometryEditState.tool !== "move") }
            Button { objectName: "geometryDeleteVertex"; text: "점 삭제"; enabled: editor.geometryEditState.selectedVertex >= 0; onClicked: editor.geometryDeleteSelectedVertex() }
            Button { objectName: "geometryUndoDraft"; text: "초안 실행 취소"; enabled: editor.geometryEditState.canUndo === true; onClicked: editor.geometryUndoDraft() }
            Button { objectName: "geometryRedoDraft"; text: "다시 실행"; visible: editor.geometryEditState.canRedo === true; onClicked: editor.geometryRedoDraft() }
            Button { objectName: "geometryPreview"; text: editor.geometryEditState.calculating ? "계산 중" : "미리보기"; enabled: editor.geometryEditState.previewReady !== true && editor.geometryEditState.calculating !== true; onClicked: editor.requestGeometryPreview() }
            Button { objectName: "geometryConfirm"; text: "확인"; enabled: editor.geometryEditState.previewReady === true; onClicked: editor.confirmGeometryEdit() }
            Button { objectName: "geometryCancel"; text: "취소"; onClicked: editor.cancelGeometryEdit() }
        }
    }
    Label {
        z: editor.layers.length+2
        anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 12
        text: "Made with Natural Earth · 5개국 예제"; color: "#526377"; font.pixelSize: 11
    }
}
