.pragma library
// Presentation-only tokens derived from web design-tokens at 28e3708.
function colors(p) {
    const dark=p.effectiveTheme==="dark"
    const accents=dark?{red:"#ff7078",orange:"#f4a24c",green:"#58c97f",teal:"#3ac5bb",blue:"#70a6ff",purple:"#ad8cff",pink:"#ef78ab"}:{red:"#d43d45",orange:"#dc781d",green:"#2c9857",teal:"#168f8b",blue:"#316fd3",purple:"#7856d6",pink:"#cc4b83"}
    return {panel:dark?"#171b20":"#ffffff",background:dark?"#101316":"#f5f5f5",input:dark?"#111519":"#ffffff",subtle:dark?"#1d2329":"#f5f5f5",hover:dark?"#222931":"#ededed",border:dark?"#29313a":"#d1d1d1",text:dark?"#edf1f5":"#242424",muted:dark?"#87939f":"#626262",accent:accents[p.accentPreset]||accents.blue,selected:dark?"#274c80":"#e9f0fc"}
}
