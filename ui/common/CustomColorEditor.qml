import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "ColorMath.js" as ColorMath

ColumnLayout {
    id: custom
    objectName: "customColorEditor"
    signal applyRequested(string value)
    signal cancelRequested()
    property var hsv: [0,0,0]
    property string colorValue: "#000000"
    property string valueFormat: "rgb"
    property bool valid: true
    readonly property bool eyeBusy: editor.screenColorPicker.busy
    property bool updating: false
    property bool pointerActive: false
    property string message: ""
    spacing: 8
    function render(keep) {
        updating=true
        if (keep!==hexField) hexField.text=colorValue.toUpperCase()
        const values=valueFormat==="rgb" ? ColorMath.hexToRgb(colorValue) : ColorMath.rgbToHsl(ColorMath.hexToRgb(colorValue))
        if (keep!==channel0 && keep!==channel1 && keep!==channel2) {
            channel0.text=String(Math.round(values[0]));channel1.text=String(Math.round(values[1]));channel2.text=String(Math.round(values[2]))
        }
        hue.value=hsv[0]
        updating=false
        plane.requestPaint()
    }
    function fromHex(value,keep) { colorValue=value;hsv=ColorMath.rgbToHsv(ColorMath.hexToRgb(value),hsv[0]);render(keep) }
    function open(value) { hsv=[0,0,0];valid=true;message="";fromHex(ColorMath.parseColorHex(value)||"#000000",null);hexField.forceActiveFocus() }
    function fromHsv(values) { hsv=values;valid=true;message="";colorValue=ColorMath.rgbToHex(ColorMath.hsvToRgb(hsv));render(null) }
    function channelsEdited() {
        if(updating)return
        const fields=[channel0,channel1,channel2],values=fields.map(function(f){return Number(f.text)})
        const max=valueFormat==="rgb"?[255,255,255]:[360,100,100]
        valid=fields.every(function(f,i){return f.text.trim().length>0 && Number.isFinite(values[i]) && Number.isInteger(values[i]) && values[i]>=0 && values[i]<=max[i]})
        message=valid?"":"표시된 범위 안의 숫자를 입력하세요."
        if(!valid)return
        if(valueFormat==="hsl")hsv=[values[0],hsv[1],hsv[2]]
        fromHex(ColorMath.rgbToHex(valueFormat==="rgb"?values:ColorMath.hslToRgb(values)),channel0)
    }
    Connections {
        target: editor.screenColorPicker
        function onColorSelected(value) { if(custom.visible && editor.colorEditOpen){custom.valid=true;custom.message="";custom.fromHex(value,null)} }
        function onFailed(message) { if(custom.visible)custom.message=message }
    }
    function keyboardItems() { return [closeControl,plane,hue,hexField,eyedropper,rgbFormat,hslFormat,channel0,channel1,channel2,cancelControl,applyControl].filter(function(item){return item.visible&&item.enabled}) }
    Keys.priority: Keys.BeforeItem
    Keys.onPressed: function(event) {
        if(event.key===Qt.Key_Tab || event.key===Qt.Key_Backtab) {
            const items=keyboardItems(),backward=event.key===Qt.Key_Backtab||!!(event.modifiers&Qt.ShiftModifier)
            if(items.length && ((!backward&&items[items.length-1].activeFocus)||(backward&&items[0].activeFocus))) {
                items[backward?items.length-1:0].forceActiveFocus();event.accepted=true;return
            }
        }
        if(event.key===Qt.Key_Escape || event.key===Qt.Key_Back){cancelRequested();event.accepted=true}
        else if((event.key===Qt.Key_Return || event.key===Qt.Key_Enter) && (hexField.activeFocus||channel0.activeFocus||channel1.activeFocus||channel2.activeFocus)){if(valid&&!pointerActive&&!eyeBusy)applyRequested(colorValue);event.accepted=true}
    }
    RowLayout {
        Layout.fillWidth: true
        Label { text: "사용자 지정"; font.bold: true; Layout.fillWidth: true }
        ToolButton { id:closeControl;KeyNavigation.priority:KeyNavigation.BeforeItem;KeyNavigation.backtab:applyControl.enabled?applyControl:cancelControl;objectName:"customColorClose";text: "닫기"; Accessible.name: "색상 선택 취소"; onClicked: custom.cancelRequested() }
    }
    Canvas {
        id: plane
        objectName: "colorSVPlane"
        Layout.fillWidth: true
        Layout.preferredHeight: 120
        activeFocusOnTab: true
        Accessible.role: Accessible.Slider
        Accessible.name: "채도와 명도. 좌우 방향키는 채도, 위아래 방향키는 명도 조절"
        onPaint: {
            const ctx=getContext("2d");ctx.fillStyle=Qt.hsla(custom.hsv[0]/360,1,.5,1);ctx.fillRect(0,0,width,height)
            let white=ctx.createLinearGradient(0,0,width,0);white.addColorStop(0,"white");white.addColorStop(1,"transparent");ctx.fillStyle=white;ctx.fillRect(0,0,width,height)
            let black=ctx.createLinearGradient(0,0,0,height);black.addColorStop(0,"transparent");black.addColorStop(1,"black");ctx.fillStyle=black;ctx.fillRect(0,0,width,height)
        }
        onWidthChanged: requestPaint()
        Rectangle { x:custom.hsv[1]*(plane.width-1)-5;y:(1-custom.hsv[2])*(plane.height-1)-5;width:10;height:10;radius:5;color:"transparent";border.color:"white";border.width:2 }
        MouseArea {
            enabled: !custom.eyeBusy
            anchors.fill: parent
            preventStealing: true
            function updatePoint(mouse){custom.fromHsv([custom.hsv[0],Math.max(0,Math.min(1,mouse.x/width)),1-Math.max(0,Math.min(1,mouse.y/height))])}
            onPressed: function(mouse){plane.forceActiveFocus();custom.pointerActive=true;updatePoint(mouse)}
            onPositionChanged: function(mouse){if(pressed)updatePoint(mouse)}
            onReleased: function(mouse){updatePoint(mouse);custom.pointerActive=false}
            onCanceled: custom.pointerActive=false
        }
        Keys.onPressed: function(event){
            const step = (event.modifiers & Qt.ShiftModifier) ? 0.1 : 0.01
            let s=custom.hsv[1],v=custom.hsv[2]
            if(event.key===Qt.Key_Left)s-=step;else if(event.key===Qt.Key_Right)s+=step;else if(event.key===Qt.Key_Down)v-=step;else if(event.key===Qt.Key_Up)v+=step;else return
            custom.fromHsv([custom.hsv[0],Math.max(0,Math.min(1,s)),Math.max(0,Math.min(1,v))]);event.accepted=true
        }
    }
    Slider { id:hue;enabled:!custom.eyeBusy;objectName:"colorHue";Layout.fillWidth:true;from:0;to:360;stepSize:1;Accessible.name:"색조";onMoved:custom.fromHsv([value,custom.hsv[1],custom.hsv[2]]) }
    RowLayout {
        Layout.fillWidth:true
        Rectangle { Layout.preferredWidth:32;Layout.preferredHeight:32;color:custom.colorValue;border.color:"#88939e";Accessible.role:Accessible.Graphic;Accessible.name:"선택한 색상 "+custom.colorValue.toUpperCase() }
        Label { text:"HEX" }
        TextField {
            id:hexField;objectName:"customColorHex";Layout.fillWidth:true;Layout.minimumWidth:0;maximumLength:7;selectByMouse:true;Accessible.name:"HEX 색상"
            onTextEdited:{const parsed=ColorMath.parseColorHex(text);custom.valid=!!parsed;custom.message=parsed?"":"HEX는 3자리 또는 6자리로 입력하세요.";if(parsed)custom.fromHex(parsed,hexField)}
        }
    }
        // This button samples the whole display through the platform adapter,
        // not the map raster. Unsupported hosts do not expose it.
    ToolButton { id:eyedropper;objectName:"screenEyedropper";text:"스포이트";visible:editor.screenColorPicker.available;enabled:!custom.eyeBusy;onClicked:editor.screenColorPicker.start() }
    RowLayout {
        Button { id:rgbFormat;objectName:"formatRgb";text:"RGB";checkable:true;checked:custom.valueFormat==="rgb";onClicked:{custom.valueFormat="rgb";custom.valid=true;custom.message="";custom.render(null)} }
        Button { id:hslFormat;objectName:"formatHsl";text:"HSL";checkable:true;checked:custom.valueFormat==="hsl";onClicked:{custom.valueFormat="hsl";custom.valid=true;custom.message="";custom.render(null)} }
    }
    RowLayout {
        Layout.fillWidth:true
        Repeater {
            model: [custom.valueFormat==="rgb"?"R":"H °",custom.valueFormat==="rgb"?"G":"S %",custom.valueFormat==="rgb"?"B":"L %"]
            Label { required property string modelData;Layout.fillWidth:true;text:modelData }
        }
    }
    RowLayout {
        Layout.fillWidth:true
        TextField { id:channel0;objectName:"customChannel0";Layout.fillWidth:true;Layout.minimumWidth:0;selectByMouse:true;inputMethodHints:Qt.ImhDigitsOnly;onTextEdited:custom.channelsEdited();Accessible.name:custom.valueFormat==="rgb"?"R":"H" }
        TextField { id:channel1;objectName:"customChannel1";Layout.fillWidth:true;Layout.minimumWidth:0;selectByMouse:true;inputMethodHints:Qt.ImhDigitsOnly;onTextEdited:custom.channelsEdited();Accessible.name:custom.valueFormat==="rgb"?"G":"S" }
        TextField { id:channel2;objectName:"customChannel2";Layout.fillWidth:true;Layout.minimumWidth:0;selectByMouse:true;inputMethodHints:Qt.ImhDigitsOnly;onTextEdited:custom.channelsEdited();Accessible.name:custom.valueFormat==="rgb"?"B":"L" }
    }
    Label { Layout.fillWidth:true;visible:custom.message.length>0;text:custom.message;wrapMode:Text.WordWrap;Accessible.role:Accessible.StaticText }
    RowLayout {
        Layout.fillWidth:true
        Item { Layout.fillWidth:true }
        Button { id:cancelControl;KeyNavigation.priority:KeyNavigation.BeforeItem;KeyNavigation.tab:applyControl.enabled?applyControl:closeControl;objectName:"customColorCancel";text:"취소";onClicked:custom.cancelRequested() }
        Button { id:applyControl;KeyNavigation.priority:KeyNavigation.BeforeItem;KeyNavigation.tab:closeControl;objectName:"customColorApply";text:"적용";enabled:custom.valid&&!custom.pointerActive&&!custom.eyeBusy;onClicked:custom.applyRequested(custom.colorValue) }
    }
}
