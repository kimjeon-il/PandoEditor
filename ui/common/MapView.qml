import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Shapes
import QtQuick.Dialogs as Native
import Pandoeditor.Windowing 1.0
import "UiTokens.js" as Tokens

Rectangle {
    id: view
    color: editor.appearancePreferences.effectiveTheme === "dark" ? "#101820" : "#e8eff4"
    clip: true
    readonly property var cameraState: editor.mapViewState
    readonly property real zoom: Number.isFinite(cameraState.flatZoom) ? cameraState.flatZoom : 1
    readonly property real panX: Number.isFinite(cameraState.panX) ? cameraState.panX : 0
    readonly property real panY: Number.isFinite(cameraState.panY) ? cameraState.panY : 0
    readonly property bool globeMode: editor.projectionMode === "globe"
    readonly property real globeZoom: Number.isFinite(cameraState.globeZoom) ? cameraState.globeZoom : 1
    readonly property real globeLongitude: Number.isFinite(cameraState.centerLongitude) ? cameraState.centerLongitude : 0
    readonly property real globeLatitude: Number.isFinite(cameraState.centerLatitude) ? cameraState.centerLatitude : 0
    readonly property real fitScale: Number.isFinite(cameraState.fitScale) ? cameraState.fitScale : 1
    readonly property real mapScale: Number.isFinite(cameraState.mapScale) ? cameraState.mapScale : 1
    readonly property real originX: Number.isFinite(cameraState.originX) ? cameraState.originX : 0
    readonly property real originY: Number.isFinite(cameraState.originY) ? cameraState.originY : 0
    signal objectActivated()
    signal selectionNavigationStarted()
    signal selectionPointerChanged(bool down)
    signal selectionModifiersChanged(bool additive)
    property bool mapSelectionAdditive:false
    function chooseCandidate(index,additive){selectionModifiersChanged(additive||mapSelectionAdditive);const result=editor.chooseMapCandidate(index,additive);selectionModifiersChanged(false);return result}
    property real controlsTopMargin:12
    property bool externalCommandBar:false
    function showReferenceImages(trigger) {
        const point=trigger.mapToItem(view,0,0)
        referenceImageMenu.x=Math.max(8,Math.min(point.x,view.width-referenceImageMenu.width-8))
        referenceImageMenu.y=Math.max(8,point.y-referenceImageMenu.height-8)
        referenceImageMenu.open()
    }
    property bool labelPress:false
    property var hoverLabel: ({})
    property point hoverLabelPoint:Qt.point(0,0)
    property bool hoverPreviewVisible:false
    property string hoverPreviewKey:""
    property point chooserPoint: Qt.point(0,0)
    property string mapHoverKey: ""
    readonly property bool geometryEditing: editor.geometryEditState.active === true
    focus: true
    Keys.onPressed: function(event) {
        if (!geometryEditing) return
        if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            if (taskPanel.vertices && editor.geometryDeleteSelectedVertex()) event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (editor.requestGeometryPreview()) event.accepted = true
        } else if (event.key === Qt.Key_Escape) {
            editor.geometryBack()
            event.accepted = true
        }
    }
    function dismissPopup() {
        if (objectChooser.visible) { objectChooser.close(); return true }
        return false
    }
    function invalidatePick() { editor.closeObjectChooser() }
    function syncViewport() {
        if (width <= 0 || height <= 0) return
        editor.resizeMapCamera(width,height)
    }
    onWidthChanged: { invalidatePick(); syncViewport() }
    onHeightChanged: { invalidatePick(); syncViewport() }
    Component.onCompleted: syncViewport()
    function fit() { editor.fitMapCamera() }
    function zoomAt(factor, px, py) { editor.zoomMapCameraAt(factor,px,py) }
    Connections {
        target: editor
        // Geometry edits retain the user's inspection position.  Explicit focus
        // and the '전체' button remain the only viewport-reset routes.
        function onGeometryChanged() { view.invalidatePick() }
        function onViewStateChanged() { view.invalidatePick() }
        function onGeometryEditChanged() {
            if (editor.geometryEditState.active === true && view.globeMode) editor.setProjectionMode("flat")
        }
        function onObjectChooserChanged() {
            if (editor.objectChooserOpen) objectChooser.openAt(view.chooserPoint)
            else objectChooser.close()
        }
    }
    ObjectChooser { id: objectChooser; mapView: view }

    // Terrain and reference images share the same immutable geographic view
    // snapshot as countries and picking.
    Item {
        id: mapSurface
        objectName: "retainedMapSurface"
        anchors.fill: parent
        // Keep the map in its own retained render pass. Qt can rebuild label
        // batches without re-uploading the world mesh. Include the backdrop,
        // terrain and reference images to preserve destination-color blending.
        layer.enabled: gpuMapRenderer.rendererReady
        Rectangle { anchors.fill: parent; color: view.color; z: -1 }
    Repeater {
        model: editor.terrainTiles
        delegate: GeographicImageItem {
            required property var modelData
            anchors.fill: parent
            sceneBridge: editor.mapSceneBridge
            terrainBridge: editor.terrainResourceBridge
            terrainTile: ({level: modelData.level, column: modelData.column, row: modelData.row})
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
        uploadBudgetBytes: editor.renderQuality.uploadBudgetBytes
        onFrameSampled: function(milliseconds) { editor.recordMapFrame(milliseconds) }
        onStatsChanged: editor.recordGpuResourceStats(gpuMapRenderer)
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
        // An invisible QQuickPaintedItem can still synchronize a dirty texture.
        // Do not feed the CPU fallback full-world snapshots while GPU is active.
        sceneBridge: visible ? editor.mapSceneBridge : null
        smoothLines: editor.appearancePreferences.smoothLines !== false
        z: 0
    }
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
    UiButton {outlined:true;
        id: referenceImageButton
        objectName: "referenceImageButton"
        visible:!view.externalCommandBar
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
    Repeater {
        model: editor.placedLabelModel
        id: placedLabelRepeater
        property var pendingDrags: []
        function enqueueDrag(instanceId, ref, x, y) {
            pendingDrags.push({"instanceId":instanceId,"ref":Object.assign({},ref),"x":x,"y":y})
            Qt.callLater(flushDrags)
        }
        function flushDrags() {
            // callLater coalesces calls. Keep every release, and detach the batch
            // before publication destroys delegates or reenters this queue.
            const commands=pendingDrags
            pendingDrags=[]
            for(const command of commands)
                if(editor.projectInstanceId===command.instanceId)
                    editor.setLabelMapPosition(command.ref,command.x,command.y)
        }
        delegate: Item {
            id:placedLabel
            objectName: "mapPlacedLabel"
            opacity:view.hoverPreviewVisible && view.hoverPreviewKey===modelData.ref.key?0:1
            required property var modelData
            required property real labelX
            required property real labelY
            // Keep gesture state on the delegate: adding properties to DragHandler
            // changes its QML type and hence Qt's default grab-takeover rules.
            property point finalLabelTranslation: Qt.point(0,0)
            property bool pendingLabelDrag: false
            function discardLabelDrag() { pendingLabelDrag=false; finalLabelTranslation=Qt.point(0,0) }
            width:(placedFlag.visible?placedFlag.width+(placedText.visible?modelData.flagGap:0):0)+(placedText.visible?placedText.implicitWidth:0)
            height:Math.max(placedFlag.visible?placedFlag.height:0,placedText.visible?placedText.implicitHeight:0)
            x: labelX-width/2
            y: labelY-height/2
            z: editor.layers.length+2
            Image {
                id:placedFlag
                objectName: "mapPlacedFlag"
                x:0;y:(parent.height-height)/2
                width:parent.modelData.flagWidth; height:parent.modelData.flagHeight; fillMode:Image.PreserveAspectFit
                // SVG intrinsic sizes can exceed 100 megapixels. Rasterize at
                // display resolution, including HiDPI, before texture upload.
                sourceSize: Qt.size(Math.ceil(width*Screen.devicePixelRatio), Math.ceil(height*Screen.devicePixelRatio))
                source:parent.modelData.flagVisible ? (parent.modelData.flagSource||"") : ""
                visible:!!parent.modelData.flagVisible&&source.toString()!==""&&status!==Image.Error
            }
            Label { id:placedText;objectName: "mapPlacedText"; textFormat:Text.PlainText;x:placedFlag.visible?placedFlag.width+parent.modelData.flagGap:0;y:(parent.height-height)/2;text:parent.modelData.name||""; visible:parent.modelData.nameVisible!==false; color:"#243746"; style:Text.Outline; styleColor:"#ffffff"; font.pixelSize:12; font.weight:Font.DemiBold }
            TapHandler {
                enabled:!view.geometryEditing;gesturePolicy:TapHandler.ReleaseWithinBounds
                onPressedChanged:{view.labelPress=pressed;view.selectionPointerChanged(pressed)}
                onTapped:{view.selectionNavigationStarted();const additive=!!(point.modifiers&(Qt.ControlModifier|Qt.MetaModifier|Qt.ShiftModifier));view.selectionModifiersChanged(additive);const selected=editor.selectObject(placedLabel.modelData.ref,additive?"toggle":"replace","map-label");view.selectionModifiersChanged(false);if(selected&&!additive)view.objectActivated()}
            }
            HoverHandler {
                acceptedDevices:PointerDevice.Mouse|PointerDevice.TouchPad
                enabled:!editor.mobileMode&&!view.geometryEditing
                onHoveredChanged:if(hovered){view.hoverLabel=placedLabel.modelData;view.hoverLabelPoint=Qt.point(placedLabel.labelX,placedLabel.labelY)}else if(view.hoverLabel.ref&&view.hoverLabel.ref.key===placedLabel.modelData.ref.key)view.hoverLabel=({})
            }
            DragHandler {
                enabled: !view.geometryEditing
                target: null
                onActiveChanged: if(active) { placedLabel.discardLabelDrag(); placedLabel.pendingLabelDrag=true }
                // Qt clears activeTranslation before activeChanged(false).
                onActiveTranslationChanged: if(active && placedLabel.pendingLabelDrag)
                    placedLabel.finalLabelTranslation=Qt.point(activeTranslation.x,activeTranslation.y)
                onCanceled: placedLabel.discardLabelDrag()
                onEnabledChanged: if(!enabled) placedLabel.discardLabelDrag()
                onGrabChanged: function(transition, point) {
                    if(transition!==PointerDevice.UngrabExclusive) return
                    const delta=Qt.point(placedLabel.finalLabelTranslation.x,placedLabel.finalLabelTranslation.y)
                    const commit=placedLabel.pendingLabelDrag && enabled && point.state===EventPoint.Released
                    placedLabel.discardLabelDrag()
                    // Publishing presentation rebuilds these delegates. Finish
                    // Qt's pointer delivery before allowing this handler to die.
                    if(commit && delta.x*delta.x+delta.y*delta.y>4)
                        placedLabelRepeater.enqueueDrag(editor.projectInstanceId,parent.modelData.ref,
                            (parent.labelX+delta.x-view.originX)/view.mapScale,
                            (parent.labelY+delta.y-view.originY)/view.mapScale)
                }
            }
        }
    }
    TapHandler {
        id: mapTap
        enabled: !objectChooser.visible && !view.geometryEditing && !view.labelPress
        acceptedButtons: Qt.LeftButton
        property int gestureModifiers: Qt.NoModifier
        onPressedChanged: {
            view.selectionPointerChanged(pressed)
            if(pressed){gestureModifiers=point.modifiers;view.selectionNavigationStarted()}
        }
        onTapped: function(eventPoint) {
            view.selectionNavigationStarted()
            view.chooserPoint = eventPoint.position
            view.mapSelectionAdditive=!!(gestureModifiers&(Qt.ControlModifier|Qt.MetaModifier|Qt.ShiftModifier))
            view.selectionModifiersChanged(view.mapSelectionAdditive)
            editor.beginMapSelectionScreen(eventPoint.position.x,eventPoint.position.y,
                                           !!(gestureModifiers & (Qt.ControlModifier | Qt.MetaModifier)),view.globeMode?view.globeZoom:view.zoom)
            view.selectionModifiersChanged(false)
            if(!editor.objectChooserOpen&&editor.selectionItems.length===1&&!view.mapSelectionAdditive)view.objectActivated()
        }
    }
    TapHandler {
        id: geometryTap
        enabled: view.geometryEditing && !objectChooser.visible
        acceptedButtons: Qt.LeftButton
        onTapped: function(eventPoint) {
            view.forceActiveFocus()
            if (editor.geometryEditState.stage === "setup" || editor.geometryEditState.previewReady) return
            if (editor.geometryEditState.choosingProviders) {
                editor.geometryToggleProvider(editor.pickObjectScreen(eventPoint.position.x,eventPoint.position.y,view.globeMode?view.globeZoom:view.zoom))
                return
            }
            const x=(eventPoint.position.x-view.originX)/view.mapScale
            const y=(eventPoint.position.y-view.originY)/view.mapScale
            if (editor.geometryEditState.tool === "draw" || editor.geometryEditState.tool === "annex" || editor.geometryEditState.tool === "split") editor.geometryAddPoint(x,y,(editor.mobileMode?18:10)/view.mapScale)
            else if (editor.geometryEditState.tool !== "move") editor.geometrySelectNearest(x,y,(editor.mobileMode?18:10)/view.mapScale)
        }
        onDoubleTapped: function(eventPoint) {
            if (editor.geometryEditState.stage !== "setup" && !editor.geometryEditState.choosingProviders && editor.geometryEditState.tool !== "draw" && editor.geometryEditState.tool !== "move")
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
        onActiveChanged: {
            if (active) { editor.beginMapInteraction(); editor.beginMapCameraPan() }
            else { editor.endMapCameraPan(); editor.endMapInteraction() }
        }
        onActiveTranslationChanged: if (active)
            editor.updateMapCameraPan(activeTranslation.x,activeTranslation.y)
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
        onActiveChanged: {
            if (active) { editor.beginMapInteraction(); editor.geometryEndVertexDrag(true);
                          editor.geometryEndObjectDrag(true); editor.beginMapCameraPan() }
            else { editor.endMapCameraPan(); editor.endMapInteraction() }
        }
        onActiveTranslationChanged: if (active)
            editor.updateMapCameraPan(activeTranslation.x,activeTranslation.y)
    }
    DragHandler {
        enabled: !objectChooser.visible
        acceptedButtons: Qt.MiddleButton
        target: null; maximumPointCount: 1
        onActiveChanged: {
            if(active) {
                editor.beginMapInteraction()
                if(!view.globeMode) editor.beginMapCameraPan()
            } else {
                if(!view.globeMode) editor.endMapCameraPan()
                editor.endMapInteraction()
            }
        }
        onActiveTranslationChanged: if(active && !view.globeMode)
            editor.updateMapCameraPan(activeTranslation.x,activeTranslation.y)
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
                    strokeColor: modelData.created ? "#059669" : "#d97706"; strokeWidth: 2 / view.mapScale
                    fillColor: modelData.hole || modelData.line ? "#00000000" : modelData.created ? "#6010b981" : "#60f59e0b"
                    fillRule: ShapePath.OddEvenFill; joinStyle: ShapePath.RoundJoin
                    PathSvg { path: modelData.path }
                }
            }
            Repeater {
                model: editor.geometryEditState.choosingProviders || editor.geometryEditState.stage === "setup" || editor.geometryEditState.previewReady ? [] : modelData.vertices
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
        visible:!view.externalCommandBar
        anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12; anchors.topMargin:view.controlsTopMargin; spacing: 4
        UiButton {outlined:true; focusPolicy: Qt.NoFocus; text: "+"; width: 44; Accessible.name: "확대"; onClicked: view.zoomAt(1.25,view.width/2,view.height/2) }
        UiButton {outlined:true; focusPolicy: Qt.NoFocus; text: "-"; width: 44; Accessible.name: "축소"; onClicked: view.zoomAt(0.8,view.width/2,view.height/2) }
        UiButton {outlined:true; focusPolicy: Qt.NoFocus; text: "전체"; onClicked: view.fit() }
    }
    Row {
        z: editor.layers.length+6
        anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 12; anchors.topMargin: view.controlsTopMargin
        spacing: 4
        visible: !view.externalCommandBar && !view.geometryEditing && editor.selectionItems.length === 1 && editor.selectedEditable
        UiButton {outlined:true; objectName: "beginGeometryEdit"; text: "도형 편집"; onClicked: editor.beginGeometryEdit("edit") }
        UiButton {outlined:true; objectName: "beginGeometryDraw"; text: "다시 그리기"; onClicked: editor.beginGeometryDraw() }
    }
    Rectangle {
        id: taskPanel
        objectName: "geometryToolDock"
        z: editor.layers.length+7
        anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 12
        width: Math.min(380, parent.width-24)
        height: minimized ? taskHeader.implicitHeight+24 : (view.width<800?(dragHeight>=0?dragHeight:[84,(parent.height-24)*.48,(parent.height-24)*.86][sheetLevel]):Math.min(parent.height-24, taskBody.implicitHeight+taskHeader.implicitHeight+taskFooter.implicitHeight+52))
        visible: view.geometryEditing
        color: palette.panel; border.color: palette.border; radius: 12
        readonly property var palette: Tokens.colors(editor.appearancePreferences)
        readonly property var taskState: editor.geometryEditState
        property bool minimized: false
        property int sheetLevel:1
        property real dragHeight:-1
        readonly property bool collapsed:view.width<800&&sheetLevel===0
        Behavior on height {enabled:!geometryHandle.dragging;NumberAnimation {duration:170;easing.type:Easing.BezierSpline;easing.bezierCurve:[.25,.1,.25,1,1,1]}}
        readonly property bool busy: taskState.calculating === true
        readonly property bool review: taskState.previewReady === true
        readonly property bool setup: taskState.stage === "setup"
        readonly property bool picking: taskState.choosingProviders === true
        readonly property bool editable: taskState.active === true && !busy && !review && !setup && !picking
        readonly property bool vertices: editable && ["edit","draw","annex","boundary","coast"].indexOf(taskState.tool)>=0
        readonly property string taskName: taskState.creating === true ? "새 객체 그리기" : ({edit:"모양 편집",draw:"다시 그리기",move:"객체 이동",merge:"영역 병합",annex:"영역 편입",split:"영역 분할",boundary:"공유 경계 편집",coast:"해안선 편집"})[taskState.tool] || "지도 편집"
        readonly property string stageName: busy ? "계산 중" : review ? "3단계 · 결과 검토" : setup ? "1단계 · 작업 준비" : picking ? "2단계 · 제공 영역 선택" : "2단계 · 지도 편집"
        readonly property string instruction: busy ? "결과를 계산하고 있습니다. 이전을 누르면 계산을 중단하고 초안으로 돌아갑니다." : review ? "지도에 표시된 결과를 검토한 뒤 확정하세요. 이전을 누르면 초안을 이어서 편집합니다." : setup ? "대상과 작업을 확인한 뒤 다음 단계로 진행하세요." : picking ? "대상은 고정됩니다. 지도에서 같은 종류의 제공 영역을 선택하거나 목록에서 제외하세요." : taskState.tool === "split" ? "영역을 가로지르는 절단선을 지도에서 그리세요. 새 객체로 남길 결과를 선택할 수 있습니다." : taskState.tool === "move" ? "지도에서 객체를 끌어 위치를 옮기세요." : taskState.tool === "draw" || taskState.tool === "annex" ? "지도에 점을 추가해 영역을 그린 뒤 미리보기를 누르세요." : taskState.tool === "merge" ? "선택한 제공 영역과 대상의 병합 결과를 미리보기로 확인하세요." : "지도에서 점을 끌어 편집하세요. 변을 두 번 누르면 점을 추가합니다."
        onVisibleChanged: if (!visible) minimized=false
        MouseArea { anchors.fill: parent; onWheel: wheel => wheel.accepted=true }
        ColumnLayout {
            anchors.fill: parent; anchors.margins: taskPanel.collapsed?8:12; spacing: taskPanel.collapsed?4:10
            UiSheetHandle {id:geometryHandle;objectName:"geometrySheetHandle";snapHeights:[84,(taskPanel.parent.height-24)*.48,(taskPanel.parent.height-24)*.86];panelHeight:taskPanel.height;onPanelHeightRequested:value=>taskPanel.dragHeight=value;Layout.preferredHeight:taskPanel.collapsed?20:28;visible:view.width<800 && !taskPanel.minimized;Layout.fillWidth:true;level:taskPanel.sheetLevel;onHeightChangedByUser:value=>taskPanel.sheetLevel=value}
            RowLayout {
                id: taskHeader
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 2
                    Label { objectName:"geometryTaskName"; text:taskPanel.taskName; font.bold:true; color:taskPanel.palette.text; Layout.fillWidth:true; elide:Text.ElideRight }
                    Label { text:taskPanel.stageName; color:taskPanel.palette.muted; font.pixelSize:12 }
                }
                UiButton {outlined:true; objectName:"geometryMinimize"; Layout.preferredHeight:taskPanel.collapsed?28:implicitHeight; focusPolicy:Qt.NoFocus; text:taskPanel.minimized?"펼치기":"접기"; onClicked:taskPanel.minimized=!taskPanel.minimized }
            }
            ScrollView {
                visible: !taskPanel.minimized&&!taskPanel.collapsed
                Layout.fillWidth:true; Layout.fillHeight:true
                contentWidth: availableWidth; clip:true
                ColumnLayout {
                    id:taskBody; width:parent.width; spacing:10
                    Label { text:"작업 대상"; color:taskPanel.palette.muted }
                    Repeater {
                        model:taskPanel.taskState.targets || []
                        Label { required property var modelData; Layout.fillWidth:true; text:modelData.name; textFormat:Text.PlainText; color:taskPanel.palette.text; wrapMode:Text.Wrap }
                    }
                    Label { visible:(taskPanel.taskState.providers || []).length>0 || taskPanel.picking; text:"제공 영역 · "+(taskPanel.taskState.providers || []).length; color:taskPanel.palette.muted }
                    Repeater {
                        model:taskPanel.taskState.providers || []
                        RowLayout {
                            required property var modelData
                            Layout.fillWidth:true
                            Label { Layout.fillWidth:true; text:modelData.name; textFormat:Text.PlainText; color:taskPanel.palette.text; wrapMode:Text.Wrap }
                            UiButton {outlined:true; objectName:"geometryRemoveProvider_"+modelData.id; text:"제외"; visible:taskPanel.picking && !taskPanel.busy && !taskPanel.review; onClicked:editor.geometryToggleProvider(modelData) }
                        }
                    }
                    Label { objectName:"geometryTaskInstruction"; Layout.fillWidth:true; text:taskPanel.instruction; color:taskPanel.palette.text; wrapMode:Text.Wrap }
                    Label { objectName:"geometryTaskError"; visible:!!taskPanel.taskState.error; Layout.fillWidth:true; text:taskPanel.taskState.error || ""; textFormat:Text.PlainText; color:"#d44848"; wrapMode:Text.Wrap }
                    BusyIndicator { visible:taskPanel.busy; running:visible; Layout.alignment:Qt.AlignHCenter }
                    ComboBox { objectName:"splitResultChoice"; Layout.fillWidth:true; visible:taskPanel.taskState.tool==="split"; model:["작은 부분을 새 객체로","결과 1을 새 객체로","결과 2를 새 객체로"]; currentIndex:taskPanel.taskState.splitChoice===undefined || taskPanel.taskState.splitChoice<0 ? 0 : 2-taskPanel.taskState.splitChoice; enabled:!taskPanel.busy; onActivated:editor.geometryChooseSplitResult(currentIndex-1) }
                    Flow {
                        Layout.fillWidth:true; spacing:4
                        UiButton {outlined:true; objectName:"geometryMoveObject"; visible:taskPanel.editable && taskPanel.taskState.target && taskPanel.taskState.target.domain!=="territorial" && taskPanel.taskState.tool!=="draw"; text:taskPanel.taskState.tool==="move"?"점 편집":"객체 이동"; onClicked:editor.geometrySetMoveMode(taskPanel.taskState.tool!=="move") }
                        UiButton {outlined:true; objectName:"geometryDeleteVertex"; visible:taskPanel.vertices; text:"점 삭제"; enabled:taskPanel.taskState.selectedVertex>=0; onClicked:editor.geometryDeleteSelectedVertex() }
                        UiButton {outlined:true; objectName:"geometryUndoDraft"; visible:taskPanel.editable && taskPanel.taskState.tool!=="merge"; text:"초안 되돌리기"; enabled:taskPanel.taskState.canUndo===true; onClicked:editor.geometryUndoDraft() }
                        UiButton {outlined:true; objectName:"geometryRedoDraft"; visible:taskPanel.editable && taskPanel.taskState.canRedo===true; text:"다시 실행"; onClicked:editor.geometryRedoDraft() }
                    }
                }
            }
            Rectangle { visible:!taskPanel.minimized&&!taskPanel.collapsed; Layout.fillWidth:true; implicitHeight:1; color:taskPanel.palette.border }
            Flow {
                id:taskFooter; visible:!taskPanel.minimized&&!taskPanel.collapsed
                Layout.fillWidth:true; spacing:4
                UiButton {outlined:true; objectName:"geometryBack"; text:"이전"; onClicked:editor.geometryBack() }
                UiButton {outlined:true; objectName:"geometryCancel"; text:"취소"; onClicked:editor.cancelGeometryEdit() }
                UiButton {outlined:true; objectName:"geometryAdvance"; visible:taskPanel.setup || (taskPanel.taskState.tool==="annex" && taskPanel.picking); enabled:!taskPanel.busy && (taskPanel.setup || (taskPanel.taskState.providers || []).length>0); text:"다음"; onClicked:editor.geometryAdvanceStage() }
                UiButton {outlined:true; objectName:"geometryPreview"; visible:!taskPanel.review && !taskPanel.setup && !(taskPanel.taskState.tool==="annex" && taskPanel.picking); text:taskPanel.busy?"계산 중":"미리보기"; enabled:!taskPanel.busy && (taskPanel.taskState.tool!=="merge" || (taskPanel.taskState.providers || []).length>0); onClicked:editor.requestGeometryPreview() }
                UiButton {outlined:true; objectName:"geometryConfirm"; visible:taskPanel.review; text:"확정"; enabled:taskPanel.review && !taskPanel.busy; onClicked:editor.confirmGeometryEdit() }
            }
        }
    }
    Label {
        z: editor.layers.length+2
        anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 12
        visible:false
        text:"Made with Natural Earth";color:Tokens.colors(editor.appearancePreferences).muted;font.pixelSize:11
    }
}
