import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "UiTokens.js" as Tokens

Dialog {
    id:root
    objectName:"historicalLibraryPanel"
    signal libraryFileRequested()
    signal flagPicked(string source)
    property bool flagMode:false
    property var ownershipChoices:({})
    property string selectedId:""
    property string snapshotId:""
    property string childDepth:"none"
    property bool approvePartial:false
    property bool optionsOpen:false
    property string selectedFlag:""
    readonly property var colors:Tokens.colors(editor.appearancePreferences)
    readonly property bool reviewing:!flagMode&&(editor.historicalStage==="ownership"||editor.historicalStage==="impact")
    readonly property bool busy:editor.historicalStage==="preparing"
    readonly property bool canonicalCatalog:editor.historicalCatalogStatus.entities>0
    readonly property var suggestedEvents:{editor.historicalCatalogStatus;return timePopover.visible?editor.historicalEvents(search.text):[]}
    readonly property var rows:{
        const historical=editor.historicalResults
        if(!flagMode)return historical
        const nativeFlags=editor.flagLibrary().map(function(row){return {id:"native-flag:"+row.code,code:row.code,name:row.name,type:"국가",kind:"country",flagSource:row.source,validFrom:"",validTo:""}})
        const query=search.text.toLowerCase()
        return nativeFlags.filter(function(row){return (!query||(row.name+" "+row.code).toLowerCase().indexOf(query)>=0)&&(!type.currentValue||type.currentValue==="country")&&status.currentValue!=="past"}).concat(historical.filter(function(row){return !!row.flagSource}))
    }
    modal:true;title:flagMode?"국기 라이브러리":"국가·지역 라이브러리"
    width:Math.min(760,parent?parent.width-16:760)
    height:Math.min(660,parent?parent.height-24:660)
    anchors.centerIn:parent;padding:16
    closePolicy:Popup.CloseOnEscape
    onOpened:{selectedId="";selectedFlag="";optionsOpen=false;editor.cancelHistoricalAdd();Qt.callLater(function(){search.forceActiveFocus()})}
    onClosed:{editor.cancelHistoricalAdd();optionsOpen=false}
    onRowsChanged:Qt.callLater(function(){if(selectedId&&!rows.some(function(row){return row.id===selectedId})){selectedId="";selectedFlag="";editor.cancelHistoricalAdd()}})
    function updateSearch(){editor.cancelHistoricalAdd();editor.searchHistorical(search.text,type.currentValue,status.currentValue,date.text,region.currentValue||"")}
    function chooseEventDate(value){timePopover.close();date.text=value;date.forceActiveFocus()}
    function selectAt(index){
        if(index<0||index>=rows.length||busy)return
        const row=rows[index]
        selectedId=row.id;selectedFlag=row.flagSource||"";snapshotId="";childDepth="none";approvePartial=false;ownershipChoices=({});optionsOpen=false
        results.currentIndex=index
        if(!flagMode||!row.code)editor.selectHistorical(row.id,"",date.text)
        Qt.callLater(function(){results.positionViewAtIndex(index,ListView.Contain)})
    }
    function options(){return {libraryId:selectedId,snapshotId:snapshotId,referenceDate:date.text,childDepth:childDepth,geometryVersionId:editor.historicalPreview.geometryVersionId||"",approvePartial:approvePartial,ownership:ownershipChoices}}
    function back(){editor.cancelHistoricalAdd();optionsOpen=false;Qt.callLater(function(){results.forceActiveFocus()})}
    header:Item {
        implicitHeight:48
        RowLayout {anchors.fill:parent;anchors.margins:16
            Label {text:root.title;font.pixelSize:18;font.weight:Font.DemiBold;Layout.fillWidth:true}
            UiButton {objectName:"historicalClose";symbol:"close";ToolTip.text:"라이브러리 닫기";onClicked:root.close()}
        }
    }
    background:Rectangle {color:root.colors.panel;border.color:root.colors.border;radius:12}
    contentItem:ColumnLayout {
        spacing:12
        GridLayout {
            enabled:!root.busy;visible:!root.optionsOpen&&!root.reviewing;columns:root.width<500?1:2;Layout.fillWidth:true
            ColumnLayout {Layout.fillWidth:true
                Label {text:"이름 검색";color:root.colors.muted;font.pixelSize:12}
                RowLayout {Layout.fillWidth:true;spacing:4
                    UiIcon {name:"search"}
                    UiTextField {id:search;objectName:root.flagMode?"flagLibrarySearch":"historicalSearch";Layout.fillWidth:true;placeholderText:"이름 검색";onTextChanged:root.updateSearch();Keys.onDownPressed:{results.currentIndex=0;results.forceActiveFocus()}}
                    UiButton {objectName:"historicalSearchClear";symbol:"close";visible:search.text.length>0;ToolTip.text:"라이브러리 검색어 지우기";onClicked:{search.clear();search.forceActiveFocus()}}
                }
            }
            ColumnLayout {Layout.fillWidth:true
                Label {text:"기준 연도";color:root.colors.muted;font.pixelSize:12}
                RowLayout {Layout.fillWidth:true;spacing:4
                    UiTextField {id:date;objectName:"historicalYear";Layout.fillWidth:true;placeholderText:"예: 1910";onTextChanged:{root.updateSearch();if(root.selectedId&&!root.flagMode)editor.selectHistorical(root.selectedId,"",text);else editor.cancelHistoricalAdd()}}
                    UiButton {id:timeSuggest;objectName:"historicalTimeSuggest";visible:root.canonicalCatalog;text:"시점";ToolTip.text:"주요 역사 시점 보기";onClicked:timePopover.visible?timePopover.close():timePopover.open()
                        Popup {id:timePopover;objectName:"historicalTimePopover";y:timeSuggest.height;x:timeSuggest.width-width
                            width:Math.min(360,root.width-32);height:Math.min(320,Math.max(80,eventList.contentHeight+16))
                            focus:true;closePolicy:Popup.CloseOnEscape|Popup.CloseOnPressOutside
                            contentItem:ListView {id:eventList;clip:true;model:root.suggestedEvents
                                delegate:UiButton {required property var modelData;objectName:"historicalTimeEvent_"+modelData.id;width:eventList.width;outlined:true;text:modelData.date+" · "+modelData.name
                                    onClicked:root.chooseEventDate(modelData.date)}
                                footer:Label {visible:eventList.count===0;text:"등록된 주요 사건이 없습니다."}
                            }
                        }
                    }
                }
            }
        }
        GridLayout {
            enabled:!root.busy;visible:!root.canonicalCatalog&&!root.optionsOpen&&!root.reviewing;columns:3;Layout.fillWidth:true
            ColumnLayout {Layout.fillWidth:true
                Label {text:"종류";font.pixelSize:12;color:root.colors.muted}
                UiComboBox {id:type;objectName:"historicalType";Layout.fillWidth:true;Layout.minimumWidth:0;model:[{text:"전체",value:""},{text:"국가",value:"country"},{text:"하위단위",value:"subunit"},{text:"지방",value:"region"}];textRole:"text";valueRole:"value";onActivated:root.updateSearch()}
            }
            ColumnLayout {Layout.fillWidth:true
                Label {text:"상태";font.pixelSize:12;color:root.colors.muted}
                UiComboBox {id:status;objectName:"historicalStatus";Layout.fillWidth:true;Layout.minimumWidth:0;model:[{text:"전체",value:"all"},{text:"현존",value:"current"},{text:"과거",value:"past"}];textRole:"text";valueRole:"value";onActivated:root.updateSearch()}
            }
            ColumnLayout {Layout.fillWidth:true
                Label {text:"지역";font.pixelSize:12;color:root.colors.muted}
                UiComboBox {id:region;objectName:"historicalRegion";Layout.fillWidth:true;Layout.minimumWidth:0;textRole:"name";valueRole:"id";model:{editor.historicalResults;return [{id:"",name:"전체"}].concat(editor.historicalRegionOptions())} onActivated:root.updateSearch()}
            }
        }
        ListView {
            id:results;objectName:"historicalResults";visible:!root.optionsOpen&&!root.reviewing
            Layout.fillWidth:true;Layout.fillHeight:true;clip:true;spacing:8;model:root.rows
            ScrollBar.vertical:ScrollBar{}
            section.property: root.flagMode ? "" : "lineageName"
            section.delegate: Label {
                required property string section
                width:results.width;visible:section.length>0;height:visible?30:0
                text:section;textFormat:Text.PlainText;font.weight:Font.DemiBold;color:root.colors.muted
            }
            keyNavigationEnabled:true
            Keys.onPressed:event=>{if(event.key===Qt.Key_Home){currentIndex=0;event.accepted=true}else if(event.key===Qt.Key_End){currentIndex=count-1;event.accepted=true}}
            Keys.onReturnPressed:root.selectAt(currentIndex)
            Keys.onSpacePressed:root.selectAt(currentIndex)
            delegate:Column {
                required property var modelData
                required property int index
                width:results.width;spacing:8
                UiButton {
                    objectName:root.flagMode?"flagLibraryEntry_"+(modelData.code||modelData.id):"historicalSelect_"+modelData.id
                    width:parent.width;implicitHeight:68;selected:root.selectedId===modelData.id;outlined:true
                    text:modelData.name+" · "+modelData.type
                    contentItem:RowLayout {spacing:12
                        Image {visible:!!modelData.flagSource;source:modelData.flagSource||"";sourceSize:Qt.size(48,32);fillMode:Image.PreserveAspectFit;Layout.preferredWidth:48;Layout.preferredHeight:32}
                        UiIcon {visible:!modelData.flagSource;name:modelData.kind||"country"}
                        ColumnLayout {Layout.fillWidth:true;spacing:3
                            Label {text:modelData.name;textFormat:Text.PlainText;font.weight:Font.DemiBold;elide:Text.ElideRight;Layout.fillWidth:true}
                            Label {text:modelData.type+" · "+(modelData.validFrom||"?")+"–"+(modelData.validTo||"현재");textFormat:Text.PlainText;color:root.colors.muted;font.pixelSize:12;Layout.fillWidth:true;elide:Text.ElideRight}
                        }
                    }
                    onClicked:root.selectAt(index)
                }
                Loader {
                    width:parent.width;active:root.selectedId===modelData.id&&(root.flagMode||(editor.historicalPreview.versions||[]).length>1)
                    sourceComponent:root.flagMode?flagDetail:boundaryDetail
                }
            }
            Label {anchors.centerIn:parent;visible:results.count===0;text:root.flagMode?"국기가 있는 항목이 없습니다.":"조건에 맞는 항목이 없습니다.";color:root.colors.muted;wrapMode:Text.Wrap;width:parent.width;horizontalAlignment:Text.AlignHCenter}
        }
        ScrollView {
            visible:root.optionsOpen||root.reviewing;Layout.fillWidth:true;Layout.fillHeight:true;contentWidth:availableWidth;clip:true
            ColumnLayout {width:parent.width;spacing:12
                Label {text:editor.historicalPreview.name||"추가 옵션";textFormat:Text.PlainText;font.pixelSize:17;font.weight:Font.DemiBold;Layout.fillWidth:true}
                ColumnLayout {visible:root.optionsOpen;Layout.fillWidth:true
                    Label {text:"불러올 범위"}
                    UiComboBox {objectName:"historicalChildDepth";Layout.fillWidth:true;model:[{text:"선택한 항목만",value:"none"},{text:"바로 아래 영역까지",value:"level1"},{text:"모든 하위 영역",value:"all"}];textRole:"text";valueRole:"value";currentIndex:root.childDepth==="all"?2:root.childDepth==="level1"?1:0;onActivated:{root.childDepth=currentValue;editor.cancelHistoricalAdd()}}
                    Label {text:"스냅샷";visible:editor.historicalSnapshots.length>0}
                    UiComboBox {objectName:"historicalSnapshot";visible:editor.historicalSnapshots.length>0;Layout.fillWidth:true;model:[{id:"",name:"개별 항목"}].concat(editor.historicalSnapshots);textRole:"name";valueRole:"id";onActivated:{root.snapshotId=currentValue;editor.cancelHistoricalAdd();if(currentIndex>0&&!date.text)date.text=model[currentIndex].referenceDate||""}}
                    UiSwitch {visible:!!editor.historicalPreview.partial||!!root.snapshotId;text:"누락 자료를 확인하고 부분 추가에 동의";checked:root.approvePartial;onClicked:{root.approvePartial=checked;editor.cancelHistoricalAdd()}}
                }
                HistoricalOwnershipSetup {Layout.fillWidth:true;visible:editor.historicalStage==="ownership";needed:editor.historicalOwnershipNeeded;countries:editor.historicalCountries;onChoiceChanged:function(id,choice){const next=Object.assign({},root.ownershipChoices);next[id]=choice;root.ownershipChoices=next}}
                ColumnLayout {visible:editor.historicalStage==="impact";Layout.fillWidth:true
                    Label {text:"영향 확인";font.weight:Font.DemiBold}
                    Label {Layout.fillWidth:true;text:editor.historicalImpact.summary||"";wrapMode:Text.Wrap}
                    Label {Layout.fillWidth:true;text:"영토 조정: "+(editor.historicalImpact.adjusted||[]).join(", ");wrapMode:Text.Wrap}
                    Label {Layout.fillWidth:true;text:"이름 갱신: "+(editor.historicalImpact.updated||[]).join(", ");wrapMode:Text.Wrap}
                }
            }
        }
        ColumnLayout {
            visible:!root.flagMode&&!root.optionsOpen&&!root.reviewing&&!!root.selectedId&&editor.historicalPreview.hasChildren===true
            Layout.fillWidth:true
            Label {text:"하위 항목";color:root.colors.muted}
            UiComboBox {objectName:"historicalInlineChildDepth";Layout.fillWidth:true;model:[{text:"선택한 항목만",value:"none"},{text:"바로 아래 하위 단위",value:"level1"},{text:"모든 하위 항목",value:"all"}];textRole:"text";valueRole:"value";currentIndex:root.childDepth==="all"?2:root.childDepth==="level1"?1:0;onActivated:{root.childDepth=currentValue;editor.cancelHistoricalAdd()}}
        }
        UiNotice {Layout.fillWidth:true;visible:!!editor.historicalError&&!root.flagMode;kind:"error";text:editor.historicalError}
    }
    footer:RowLayout {
        spacing:8
        UiButton {visible:!root.flagMode;symbol:"folder";text:"파일";ToolTip.text:"외부 라이브러리 파일 불러오기";onClicked:root.libraryFileRequested()}
        Item {Layout.fillWidth:true}
        UiButton {objectName:"historicalOptionsBack";visible:root.optionsOpen||root.reviewing;text:"목록";onClicked:root.back()}
        UiButton {objectName:"historicalOptionsButton";visible:!root.flagMode&&!root.optionsOpen&&!root.reviewing&&(editor.historicalSnapshots.length>0);text:"추가 옵션";onClicked:root.optionsOpen=true}
        UiButton {objectName:root.flagMode?"flagLibraryApply":"historicalAddButton";highlighted:true;text:root.flagMode?"적용":editor.historicalStage==="impact"?"확정":root.busy?"계산 중":"추가";enabled:!root.busy&&(root.snapshotId||root.selectedId);onClicked:{if(root.flagMode){root.flagPicked(root.selectedFlag);root.close()}else if(editor.historicalStage==="impact"){if(editor.confirmHistoricalAdd(editor.historicalSession))root.close()}else editor.prepareHistoricalAdd(root.options())}}
    }
    Component {id:flagDetail;ColumnLayout {Label {text:root.rows.find(function(row){return row.id===root.selectedId})?.name||"";font.weight:Font.DemiBold}Label {text:"이 항목의 기본 국기를 적용합니다.";color:root.colors.muted}}}
    Component {id:boundaryDetail;ColumnLayout {
        spacing:8
        Label {text:"경계";color:root.colors.muted}
        UiComboBox {objectName:"historicalGeometryVersion";Layout.fillWidth:true;model:editor.historicalPreview.versions||[];textRole:"label";valueRole:"id";currentIndex:{const rows=editor.historicalPreview.versions||[];const index=rows.findIndex(function(row){return row.id===editor.historicalPreview.geometryVersionId});return Math.max(0,index)} onActivated:editor.selectHistorical(root.selectedId,currentValue,date.text)}
        HistoricalEntityPreview {Layout.fillWidth:true;preview:editor.historicalPreview}
    }}
}
