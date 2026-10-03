import QtQuick
import QtQuick.Shapes
Item {
    id:icon
    property string name:""
    property color color:"#626262"
    implicitWidth:18;implicitHeight:18
    readonly property var paths:({undo:"M9 5 L4 10 L9 15 M4 10 H13 Q20 10 20 18",redo:"M15 5 L20 10 L15 15 M20 10 H11 Q4 10 4 18",search:"M10 3 A7 7 0 1 0 10 17 A7 7 0 1 0 10 3 M15 15 L21 21",plus:"M12 4 V20 M4 12 H20",close:"M6 6 L18 18 M18 6 L6 18",image:"M4 4 H20 V20 H4 Z M4 16 L9 11 L14 16 L17 13 L20 16 M15 7 H16 V8 H15 Z",focus:"M4 9 V4 H9 M15 4 H20 V9 M20 15 V20 H15 M9 20 H4 V15 M8 8 H16 V16 H8 Z",edit:"M4 20 L5 15 L16 4 L20 8 L9 19 Z M14 6 L18 10",notes:"M5 3 H19 V21 H5 Z M8 8 H16 M8 12 H16 M8 16 H13",eye:"M2 12 Q12 0 22 12 Q12 24 2 12 Z M12 8 A4 4 0 1 0 12 16 A4 4 0 1 0 12 8",lock:"M6 10 H18 V21 H6 Z M8 10 V6 A4 4 0 0 1 16 6 V10",unlock:"M6 10 H18 V21 H6 Z M8 10 V6 A4 4 0 0 1 16 6",globe:"M12 2 A10 10 0 1 0 12 22 A10 10 0 1 0 12 2 M2 12 H22 M12 2 Q4 12 12 22 Q20 12 12 2",minus:"M4 12 H20"})
    Shape {
        width:24;height:24;scale:icon.width/24;transformOrigin:Item.TopLeft
        ShapePath {strokeColor:icon.color;strokeWidth:1.6;fillColor:"transparent";capStyle:ShapePath.RoundCap;joinStyle:ShapePath.RoundJoin;PathSvg {path:icon.paths[icon.name]||""}}
    }
}
