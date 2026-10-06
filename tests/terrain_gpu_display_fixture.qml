import QtQuick
import QtQuick.Window
import Pandoeditor.TerrainTest 1.0

// Real production providers/materials. No software pixel renderer or shader mock.
Rectangle {
    id: surface
    width: 384
    height: 192
    color: "#11171f"
    clip: true
    property real panPixels: 0
    property string terrainColorMode: "color"
    property bool maskLive: true
    property bool maskEnabled: true
    property bool terrainEnabled: true
    property bool terrainTileValid: true

    TerrainLandMaskItem {
        id: physicalLandMask
        objectName: "physicalLandMask"
        anchors.fill: parent
        sceneBridge: fixtureSceneBridge
        textureSource: physicalLandTexture
        originX: surface.panPixels
        originY: 0
        mapScale: surface.width / 360
        mapCosLatitude: 1
        mapMinX: -180
        mapMaxLatitude: 90
    }

    // Zero display area prevents an ImageNode from updating the layer first.
    // The terrain node orders updateTexture() before sampling this same frame.
    ShaderEffectSource {
        id: physicalLandTexture
        objectName: "physicalLandTexture"
        width: 0
        height: 0
        sourceItem: physicalLandMask
        hideSource: true
        live: surface.maskLive
        smooth: false
        mipmap: false
        recursive: false
        sourceRect: Qt.rect(0, 0, surface.width, surface.height)
        textureSize: Qt.size(Math.ceil(surface.width * Screen.devicePixelRatio),
                             Math.ceil(surface.height * Screen.devicePixelRatio))
    }

    Repeater {
        model: surface.terrainEnabled ? fixtureTiles : []
        delegate: GeographicImageItem {
            anchors.fill: surface
            sceneBridge: fixtureSceneBridge
            terrainBridge: fixtureTerrainBridge
            terrainTile: surface.terrainTileValid ? ({ level: modelData.level, column: modelData.column, row: modelData.row }) : ({})
            west: modelData.west
            east: modelData.east
            south: modelData.south
            north: modelData.north
            colorMode: surface.terrainColorMode
            landMaskSource: surface.maskEnabled ? physicalLandMask : null
        }
    }
}
