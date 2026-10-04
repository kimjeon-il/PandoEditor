import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    property var needed: []
    property var countries: []
    property var choices: ({})
    signal choiceChanged(string id, var choice)
    spacing: 8
    Repeater {
        model: root.needed
        delegate: GroupBox {
            id: group
            required property var modelData
            Layout.fillWidth: true
            title: modelData.name + " · 소속 설정"
            ColumnLayout {
                anchors.fill: parent
                UiComboBox {
                    id: mode
                    Layout.fillWidth: true
                    model: ["기존 국가의 하위단위로 추가", "독립 국가로 추가"]
                    onCurrentIndexChanged: root.choiceChanged(group.modelData.id,
                        {mode: currentIndex===1 ? "country" : "subunit",
                         parentId: parentUnit.currentValue,name: countryName.text})
                }
                UiComboBox {
                    id: country
                    Layout.fillWidth: true
                    visible: mode.currentIndex===0
                    textRole: "name"; valueRole: "id"
                    model: [{id:"",name:"행정 root 선택"}].concat(root.countries)
                    onActivated: {
                        parentUnit.currentIndex=0
                        root.choiceChanged(group.modelData.id,
                            {mode:"subunit",parentId:"",name:countryName.text})
                    }
                }
                UiComboBox {
                    id: parentUnit
                    Layout.fillWidth: true
                    visible: mode.currentIndex===0
                    textRole: "name"; valueRole: "id"
                    model: [{id:"",name:"상위 단위 선택"}].concat(editor.historicalParents(country.currentValue))
                    onActivated: root.choiceChanged(group.modelData.id,
                        {mode:"subunit",parentId:currentValue,name:countryName.text})
                }
                UiTextField {
                    id: countryName
                    Layout.fillWidth: true
                    visible: mode.currentIndex===1
                    text: modelData.name
                    placeholderText: "새 국가 이름"
                    onTextChanged: root.choiceChanged(group.modelData.id,
                        {mode:"country",parentId:"",name:text})
                }
            }
        }
    }
}
