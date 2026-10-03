import QtQuick
import QtQuick.Shapes
Item {
    id:icon
    property string name:""
    property color color:"#626262"
    implicitWidth:18;implicitHeight:18
    readonly property var paths:({chevronDown:"M5 9 L12 16 L19 9",undo:"M9 5 L4 10 L9 15 M4 10 H13 Q20 10 20 18",redo:"M15 5 L20 10 L15 15 M20 10 H11 Q4 10 4 18",search:"M10 3 A7 7 0 1 0 10 17 A7 7 0 1 0 10 3 M15 15 L21 21",plus:"M12 4 V20 M4 12 H20",close:"M6 6 L18 18 M18 6 L6 18",image:"M4 4 H20 V20 H4 Z M4 16 L9 11 L14 16 L17 13 L20 16 M15 7 H16 V8 H15 Z",focus:"M4 9 V4 H9 M15 4 H20 V9 M20 15 V20 H15 M9 20 H4 V15 M8 8 H16 V16 H8 Z",edit:"M4 20 L5 15 L16 4 L20 8 L9 19 Z M14 6 L18 10",notes:"M5 3 H19 V21 H5 Z M8 8 H16 M8 12 H16 M8 16 H13",eyeOff:"M2 12 Q12 0 22 12 Q12 24 2 12 Z M3 3 L21 21",eye:"M2 12 Q12 0 22 12 Q12 24 2 12 Z M12 8 A4 4 0 1 0 12 16 A4 4 0 1 0 12 8",lock:"M6 10 H18 V21 H6 Z M8 10 V6 A4 4 0 0 1 16 6 V10",unlock:"M6 10 H18 V21 H6 Z M8 10 V6 A4 4 0 0 1 16 6",globe:"M12 2 A10 10 0 1 0 12 22 A10 10 0 1 0 12 2 M2 12 H22 M12 2 Q4 12 12 22 Q20 12 12 2",minus:"M4 12 H20",back:"M15 5 L8 12 L15 19",flat:"M3 4 H21 V20 H3 Z M3 10 H21 M3 15 H21 M9 4 V20 M15 4 V20",country:"M5 21 V3 M5 4 H19 L16 9 L19 14 H5",subunit:"M4 5 H20 V20 H4 Z M4 12 H20 M12 5 V20",region:"M5 5 L12 2 L20 7 L18 18 L9 22 L3 15 Z",place:"M12 22 Q4 14 4 9 A8 8 0 0 1 20 9 Q20 14 12 22 M12 6 A3 3 0 1 0 12 12 A3 3 0 1 0 12 6",river:"M4 3 C20 7 3 14 20 21",lake:"M5 8 Q9 3 14 6 Q22 5 20 14 Q17 22 11 18 Q3 20 4 12 Z",distribution:"M4 20 V12 H8 V20 M10 20 V7 H14 V20 M16 20 V3 H20 V20",type:"M4 5 H20 M12 5 V20 M8 20 H16",terrain:"M2 20 L9 5 L14 13 L18 8 L23 20 Z",folder:"M3 6 H10 L12 9 H21 V20 H3 Z",save:"M4 3 H18 L21 6 V21 H4 Z M8 3 V9 H16 V3 M8 21 V14 H17 V21",import:"M3 4 H14 V9 M3 4 V20 H21 V14 M12 12 H23 M19 8 L23 12 L19 16",export:"M3 4 H14 V9 M3 4 V20 H21 V14 M23 12 H12 M16 8 L12 12 L16 16",library:"M3 4 H8 V21 H3 Z M10 4 H15 V21 H10 Z M17 5 L21 4 L24 20 L20 21 Z"})
    Shape {
        width:24;height:24;scale:icon.width/24;transformOrigin:Item.TopLeft
        ShapePath {strokeColor:icon.color;strokeWidth:1.6;fillColor:"transparent";capStyle:ShapePath.RoundCap;joinStyle:ShapePath.RoundJoin;PathSvg {path:icon.paths[icon.name]||""}}
    }
}
