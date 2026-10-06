export const TERRAIN_RASTER_VERSION = '0.12.6';
export const TERRAIN_RASTER_DATASET = 'Natural Earth raster 3.2.0 1:10m';
export const TERRAIN_DEM_VERSION = '0.13.3';
export const TERRAIN_DEM_FORMAT = 'dem-relief-v1';
export const TERRAIN_RASTER_FORMAT = 'raster-rgba-v1';

export function terrainRasterManifestUrl(baseUrl, revision = '') {
  const url = new URL(`terrain/v${TERRAIN_RASTER_VERSION}/manifest.json`, baseUrl);
  if (revision) url.searchParams.set('v', revision);
  return url;
}

export function selectTerrainManifestUrls({ official, raster, dev = null, preview = false, rasterOnly = false, defaultDem = false }) {
  if (dev) return { primary: dev, fallback: raster, source: 'development' };
  if (rasterOnly) return { primary: raster, fallback: null, source: 'raster' };
  if (preview || defaultDem) return { primary: official, fallback: raster, source: preview ? 'preview' : 'official' };
  return { primary: raster, fallback: null, source: 'raster' };
}

export function terrainAssetUrl(path, { manifestUrl, dataBaseUrl, revision = '' } = {}) {
  const value = String(path || '');
  if (!value) throw new Error('지형 자산 주소가 없습니다.');
  const root = manifestUrl ? new URL('../../', manifestUrl) : dataBaseUrl;
  const url = new URL(value, root);
  if (revision && url.origin === new URL(root).origin) url.searchParams.set('v', revision);
  return url;
}

export function validateTerrainManifest(manifest) {
  const format = manifest?.representation || (manifest?.version === TERRAIN_RASTER_VERSION ? TERRAIN_RASTER_FORMAT : '');
  if (format !== TERRAIN_RASTER_FORMAT && format !== TERRAIN_DEM_FORMAT) {
    throw new Error(`지원하지 않는 지형 표현 형식: ${format || '(없음)'}`);
  }
  if (manifest.crs !== 'EPSG:4326' || JSON.stringify(manifest.extent) !== '[-180,-90,180,90]') {
    throw new Error('지형 좌표계 또는 범위가 올바르지 않습니다.');
  }
  if (!Array.isArray(manifest.levels) || !manifest.levels.length || Number(manifest.gutter) !== 1
      || !String(manifest.urlTemplate || '').includes('{level}')
      || !String(manifest.urlTemplate || '').includes('{column}')
      || !String(manifest.urlTemplate || '').includes('{row}')) {
    throw new Error('지형 타일 격자 또는 주소가 올바르지 않습니다.');
  }
  for (const [index, level] of manifest.levels.entries()) {
    const width = Number(level.width), height = Number(level.height), tileSize = Number(level.tileSize);
    if (level.id !== index || !Number.isInteger(width) || !Number.isInteger(height)
        || !Number.isInteger(tileSize) || tileSize < 1 || tileSize > 1024
        || Number(level.columns) !== Math.ceil(width / tileSize)
        || Number(level.rows) !== Math.ceil(height / tileSize)) {
      throw new Error(`지형 LOD ${index}의 크기가 올바르지 않습니다.`);
    }
  }
  if (format === TERRAIN_DEM_FORMAT) {
    const resolution = manifest.sourceResolutionDegrees;
    // The representation owns the decoding contract; a data-only patch release
    // may change tint/checksums without changing the official default URL.
    if (!/^\d+\.\d+\.\d+$/.test(String(manifest.version || '')) || manifest.tileFormat !== 'lossless WebP RGBA'
        || manifest.registration !== 'cell-center' || manifest.elevation?.decode !== 'R*256+G-12000'
        || JSON.stringify(manifest.sourceGridOrigin) !== '[-180,90]'
        || !Array.isArray(resolution) || resolution.length !== 2
        || resolution.some(value => Math.abs(Number(value) - 1/120) > 1e-12)
        || manifest.elevation?.biasMeters !== 12000 || manifest.elevation?.spacingMeters !== 1
        || JSON.stringify(manifest.elevation?.validEncodedRange) !== '[0,65535]'
        || manifest.channels?.r !== 'encoded elevation high byte'
        || manifest.channels?.g !== 'encoded elevation low byte'
        || manifest.channels?.b !== 'precomputed hillshade' || manifest.channels?.a !== '255'
        || manifest.shade?.azimuthDegrees !== 315 || manifest.shade?.altitudeDegrees !== 45
        || manifest.shade?.ambient !== 0.42 || manifest.shade?.diffuse !== 0.58
        || manifest.shade?.polarFallbackDegrees !== 89.5
        || (manifest.shade?.quantizationStep !== undefined && manifest.shade.quantizationStep !== 4)
        || manifest.levels.length !== 6
        || manifest.levels.some((level, index) => level.width !== 1350 * 2 ** index
          || level.height !== 675 * 2 ** index)
        || !manifest.tint?.url || !Number.isInteger(manifest.tint.width) || !Number.isInteger(manifest.tint.height)
        || manifest.tint.height < 1 || manifest.tint.width !== manifest.tint.height * 2
        || !manifest.assetsSha256 || !Array.isArray(manifest.sources)) {
      throw new Error('DEM 지형 채널·고도·LOD 계약이 올바르지 않습니다.');
    }
  }
  return Object.freeze({ ...manifest, representation: format });
}
