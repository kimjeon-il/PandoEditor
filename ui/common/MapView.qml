import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
import QtQuick.Dialogs as Native
import Pandoeditor.Windowing 1.0

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
    Timer {
        id: hydroRequest
        interval: 40; repeat: false
        onTriggered: editor.requestHydroViewport(view.zoom,view.mapScale,view.originX,view.originY,view.width,view.height)
    }
    onZoomChanged: { invalidatePick(); hydroRequest.restart() }
    onPanXChanged: { invalidatePick(); hydroRequest.restart() }
    onPanYChanged: { invalidatePick(); hydroRequest.restart() }
    onWidthChanged: { invalidatePick(); hydroRequest.restart() }
    onHeightChanged: { invalidatePick(); hydroRequest.restart() }
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
        // Geometry edits retain the user's inspection position.  Explicit focus
        // and the '전체' button remain the only viewport-reset routes.
        function onGeometryChanged() { view.invalidatePick(); hydroRequest.restart() }
        function onStateChanged() { hydroRequest.restart() }
        function onFocusRequested(left,top,width,height,maxZoom) { view.focusRect(left,top,width,height,maxZoom) }
        function onObjectChooserChanged() {
            if (editor.objectChooserOpen) objectChooser.openAt(view.chooserPoint)
            else objectChooser.close()
        }
    }
    ObjectChooser { id: objectChooser; mapView: view }

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
            visible: modelData.visible
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

    MapRenderItem {
        id: canonicalMapRenderer
        objectName: "canonicalMapRenderer"
        anchors.fill: parent
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
        return editor.labelLayout(mapScale,originX,originY,zoom,width,height)
    }
    Repeater {
        model: view.placedLabels
        delegate: Column {
            required property var modelData
            x: modelData.x-width/2
            y: modelData.y-height/2
            z: editor.layers.length+2
            Image { anchors.horizontalCenter: parent.horizontalCenter; width:24; height:16; fillMode:Image.PreserveAspectFit; source:parent.modelData.flagSource||""; visible:!!parent.modelData.flagVisible&&source.toString()!=="" }
            Label { textFormat:Text.PlainText; anchors.horizontalCenter:parent.horizontalCenter; text:parent.modelData.name||""; color:"#243746"; font.pixelSize:12 }
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
            editor.beginMapSelection((eventPoint.position.x-view.originX)/view.mapScale,
                                     (eventPoint.position.y-view.originY)/view.mapScale,
                                     !!(gestureModifiers & (Qt.ControlModifier | Qt.MetaModifier)),view.mapScale,view.zoom)
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
            else editor.geometrySelectNearest(x,y,(editor.mobileMode?18:10)/view.mapScale)
        }
        onDoubleTapped: function(eventPoint) {
            if (editor.geometryEditState.tool !== "draw")
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
            const ref=editor.pickObject((point.position.x-view.originX)/view.mapScale,
                                        (point.position.y-view.originY)/view.mapScale, view.mapScale,view.zoom)
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
        onActiveChanged: if (active) { startX=view.panX; startY=view.panY }
        onActiveTranslationChanged: if (active) { view.panX=startX+activeTranslation.x; view.panY=startY+activeTranslation.y }
    }
    DragHandler {
        id: geometryDrag
        enabled: view.geometryEditing && editor.geometryEditState.selectedVertex >= 0 && !objectChooser.visible
        target: null
        maximumPointCount: 1
        onActiveChanged: { if (active) editor.geometryBeginVertexDrag(); else editor.geometryEndVertexDrag(false) }
        onActiveTranslationChanged: if (active) {
            editor.geometryMoveSelectedVertex((centroid.position.x-view.originX)/view.mapScale,
                                              (centroid.position.y-view.originY)/view.mapScale,
                                              (editor.mobileMode?18:10)/view.mapScale)
        }
    }
    DragHandler {
        id: geometryTwoFingerPan
        enabled: view.geometryEditing && !objectChooser.visible
        target: null; minimumPointCount: 2; maximumPointCount: 2
        property real startX: 0; property real startY: 0
        onActiveChanged: if (active) { editor.geometryEndVertexDrag(true); startX=view.panX; startY=view.panY }
        onActiveTranslationChanged: if (active) { view.panX=startX+activeTranslation.x; view.panY=startY+activeTranslation.y }
    }
    DragHandler {
        enabled: !objectChooser.visible
        acceptedButtons: Qt.MiddleButton
        target: null; maximumPointCount: 1
        property real startX: 0; property real startY: 0
        onActiveChanged: if(active){startX=view.panX;startY=view.panY}
        onActiveTranslationChanged: if(active){view.panX=startX+activeTranslation.x;view.panY=startY+activeTranslation.y}
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
            Label { visible: editor.geometryEditState.tool !== "draw"; text: "변을 두 번 탭해 점 추가"; verticalAlignment: Text.AlignVCenter }
            Button { objectName: "geometryDeleteVertex"; text: "점 삭제"; enabled: editor.geometryEditState.selectedVertex >= 0; onClicked: editor.geometryDeleteSelectedVertex() }
            Button { objectName: "geometryUndoDraft"; text: "초안 실행 취소"; enabled: editor.geometryEditState.canUndo === true; onClicked: editor.geometryUndoDraft() }
            Button { objectName: "geometryRedoDraft"; text: "다시 실행"; visible: editor.geometryEditState.canRedo === true; onClicked: editor.geometryRedoDraft() }
            Button { objectName: "geometryPreview"; text: "미리보기"; enabled: editor.geometryEditState.previewReady !== true; onClicked: editor.requestGeometryPreview() }
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
