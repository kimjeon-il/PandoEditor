import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    property var needed: []
    property var countries: []
    property var choices: ({})
    readonly property bool canonical: editor.historicalCatalogStatus.entities>0
    function choice(independent,parentId,countryId,name) {
        return {mode: independent ? (canonical ? "root" : "country") : (canonical ? "child" : "subunit"),
                parentId:parentId,countryId:countryId,name:name}
    }
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
                        root.choice(currentIndex===1,parentUnit.currentValue,country.currentValue,countryName.text))
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
                            root.choice(false,"",currentValue,countryName.text))
                    }
                }
                UiComboBox {
                    id: parentUnit
                    Layout.fillWidth: true
                    visible: mode.currentIndex===0
                    textRole: "name"; valueRole: "id"
                    model: [{id:"",name:"상위 단위 선택"}].concat(editor.historicalParents(country.currentValue))
                    onActivated: root.choiceChanged(group.modelData.id,
                        root.choice(false,currentValue,country.currentValue,countryName.text))
                }
                UiTextField {
                    id: countryName
                    Layout.fillWidth: true
                    visible: mode.currentIndex===1
                    text: modelData.name
                    placeholderText: "새 국가 이름"
                    onTextChanged: root.choiceChanged(group.modelData.id,
                        root.choice(true,"","",text))
                }
            }
        }
    }
}
