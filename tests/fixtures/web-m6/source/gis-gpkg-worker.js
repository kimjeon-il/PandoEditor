'use strict';

const WORKER_REVISION = new URL(self.location.href).searchParams.get('v') || '';
const buildMetaUrl = new URL('../build-meta.js', self.location.href);
buildMetaUrl.searchParams.set('v', WORKER_REVISION);
importScripts(buildMetaUrl.href);
const resolvedWorkerRevision = globalThis.PANDOLAB_BUILD_META?.assetRevision || WORKER_REVISION;
const GIS_ADAPTER_URL = new URL('../gis-adapters.js', self.location.href);
if (resolvedWorkerRevision) GIS_ADAPTER_URL.searchParams.set('v', resolvedWorkerRevision);
importScripts(GIS_ADAPTER_URL.href);

const SQL_SCRIPT_URL = new URL('../vendor/sql/sql-wasm.js', self.location.href).href;
const SQL_WASM_URL = new URL('../vendor/sql/sql-wasm.wasm', self.location.href).href;
let sqlPromise = null;

function getSql() {
  if (!sqlPromise) {
    importScripts(SQL_SCRIPT_URL);
    sqlPromise = initSqlJs({ locateFile: () => SQL_WASM_URL });
  }
  return sqlPromise;
}

function scalar(value) {
  if (value == null) return null;
  if (typeof value === 'string' || typeof value === 'number') return value;
  if (typeof value === 'boolean') return value ? 1 : 0;
  return JSON.stringify(value);
}

function base64ToBytes(value) {
  const clean = String(value || '').replace(/\s+/g, '');
  const binary = atob(clean);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i += 1) bytes[i] = binary.charCodeAt(i);
  return bytes;
}

function bytesToBase64(bytes) {
  let binary = '';
  const chunk = 0x8000;
  for (let i = 0; i < bytes.length; i += chunk) {
    binary += String.fromCharCode(...bytes.subarray(i, i + chunk));
  }
  return btoa(binary);
}

function geometryBounds(geometry) {
  const bounds = [Infinity, Infinity, -Infinity, -Infinity];
  const visit = value => {
    if (!Array.isArray(value)) return;
    if (value.length >= 2 && Number.isFinite(+value[0]) && Number.isFinite(+value[1])) {
      const x = +value[0], y = +value[1];
      bounds[0] = Math.min(bounds[0], x);
      bounds[1] = Math.min(bounds[1], y);
      bounds[2] = Math.max(bounds[2], x);
      bounds[3] = Math.max(bounds[3], y);
      return;
    }
    value.forEach(visit);
  };
  visit(geometry?.coordinates);
  return bounds.every(Number.isFinite) ? bounds : null;
}

function pushUint32(out, value) {
  const bytes = new Uint8Array(4);
  new DataView(bytes.buffer).setUint32(0, value >>> 0, true);
  out.push(...bytes);
}

function pushInt32(out, value) {
  const bytes = new Uint8Array(4);
  new DataView(bytes.buffer).setInt32(0, value | 0, true);
  out.push(...bytes);
}

function pushFloat64(out, value) {
  const bytes = new Uint8Array(8);
  new DataView(bytes.buffer).setFloat64(0, +value, true);
  out.push(...bytes);
}

function writeWkbGeometry(out, geometry) {
  const typeCodes = {
    Point: 1,
    LineString: 2,
    Polygon: 3,
    MultiPoint: 4,
    MultiLineString: 5,
    MultiPolygon: 6,
  };
  const type = geometry?.type;
  const code = typeCodes[type];
  if (!code) throw new Error(`지원되지 않는 GeoPackage 지오메트리: ${type || '없음'}`);
  out.push(1);
  pushUint32(out, code);
  const coords = geometry.coordinates;
  if (type === 'Point') {
    pushFloat64(out, coords[0]);
    pushFloat64(out, coords[1]);
  } else if (type === 'LineString') {
    pushUint32(out, coords.length);
    coords.forEach(point => { pushFloat64(out, point[0]); pushFloat64(out, point[1]); });
  } else if (type === 'Polygon') {
    pushUint32(out, coords.length);
    coords.forEach(ring => {
      pushUint32(out, ring.length);
      ring.forEach(point => { pushFloat64(out, point[0]); pushFloat64(out, point[1]); });
    });
  } else if (type === 'MultiPoint') {
    pushUint32(out, coords.length);
    coords.forEach(point => writeWkbGeometry(out, { type: 'Point', coordinates: point }));
  } else if (type === 'MultiLineString') {
    pushUint32(out, coords.length);
    coords.forEach(line => writeWkbGeometry(out, { type: 'LineString', coordinates: line }));
  } else if (type === 'MultiPolygon') {
    pushUint32(out, coords.length);
    coords.forEach(polygon => writeWkbGeometry(out, { type: 'Polygon', coordinates: polygon }));
  }
}

function encodeGeoPackageGeometry(geometry, srsId = 4326) {
  const out = [0x47, 0x50, 0, 1];
  pushInt32(out, srsId);
  writeWkbGeometry(out, geometry);
  return new Uint8Array(out);
}

function promoteGeometry(geometry, targetType) {
  if (!geometry) return null;
  if (targetType === 'MULTILINESTRING') {
    if (geometry.type === 'MultiLineString') return geometry;
    if (geometry.type === 'LineString') return { type: 'MultiLineString', coordinates: [geometry.coordinates] };
  }
  if (targetType === 'MULTIPOLYGON') {
    if (geometry.type === 'MultiPolygon') return geometry;
    if (geometry.type === 'Polygon') return { type: 'MultiPolygon', coordinates: [geometry.coordinates] };
  }
  return geometry.type.toUpperCase() === targetType ? geometry : null;
}

function insertContents(db, tableName, dataType, description, bounds = null) {
  const now = new Date().toISOString();
  db.run(
    'INSERT OR REPLACE INTO gpkg_contents (table_name, data_type, identifier, description, last_change, min_x, min_y, max_x, max_y, srs_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)',
    [tableName, dataType, tableName, description || '', now, bounds?.[0] ?? null, bounds?.[1] ?? null, bounds?.[2] ?? null, bounds?.[3] ?? null, dataType === 'features' ? 4326 : null],
  );
}

function createFeatureTable(db, { tableName, geometryType, rows, columns, description }) {
  db.run(`DROP TABLE IF EXISTS "${tableName}"`);
  db.run(`DELETE FROM gpkg_geometry_columns WHERE table_name = ?`, [tableName]);
  db.run(`DELETE FROM gpkg_contents WHERE table_name = ?`, [tableName]);
  const columnSql = columns.map(column => `"${column.name}" ${column.type || 'TEXT'}`).join(', ');
  db.run(`CREATE TABLE "${tableName}" (fid INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL, geom BLOB NOT NULL${columnSql ? `, ${columnSql}` : ''})`);
  const allBounds = [Infinity, Infinity, -Infinity, -Infinity];
  const placeholders = ['?', ...columns.map(() => '?')].join(', ');
  const statement = db.prepare(`INSERT INTO "${tableName}" (geom${columns.length ? `, ${columns.map(column => `"${column.name}"`).join(', ')}` : ''}) VALUES (${placeholders})`);
  for (const row of rows) {
    const geometry = promoteGeometry(row.geometry, geometryType);
    if (!geometry) continue;
    const bounds = geometryBounds(geometry);
    if (bounds) {
      allBounds[0] = Math.min(allBounds[0], bounds[0]);
      allBounds[1] = Math.min(allBounds[1], bounds[1]);
      allBounds[2] = Math.max(allBounds[2], bounds[2]);
      allBounds[3] = Math.max(allBounds[3], bounds[3]);
    }
    statement.run([encodeGeoPackageGeometry(geometry), ...columns.map(column => scalar(row[column.source || column.name]))]);
  }
  statement.free();
  const finalBounds = allBounds.every(Number.isFinite) ? allBounds : null;
  insertContents(db, tableName, 'features', description, finalBounds);
  db.run('INSERT INTO gpkg_geometry_columns (table_name, column_name, geometry_type_name, srs_id, z, m) VALUES (?, ?, ?, 4326, 0, 0)', [tableName, 'geom', geometryType]);
}

function createAttributeTable(db, tableName, schema, description) {
  db.run(`DROP TABLE IF EXISTS "${tableName}"`);
  db.run('DELETE FROM gpkg_contents WHERE table_name = ?', [tableName]);
  db.run(`CREATE TABLE "${tableName}" (${schema})`);
  insertContents(db, tableName, 'attributes', description, null);
}

function removeTable(db, tableName) {
  db.run(`DROP TABLE IF EXISTS "${tableName}"`);
  db.run('DELETE FROM gpkg_geometry_columns WHERE table_name = ?', [tableName]);
  db.run('DELETE FROM gpkg_contents WHERE table_name = ?', [tableName]);
}

function writeAtlasTables(db, payload) {
  const state = payload.projectState || {};
  const gisMode = payload.exportMode === 'gis';
  const selected = new Set(payload.selectedLayers || []);
  const includes = layer => !gisMode || selected.has(layer);
  if (gisMode) removeTable(db, 'pandolab_export_seed');
  const labels = includes('labels') ? state.labels || [] : [];
  const genericFeatures = includes('genericFeatures') ? state.genericFeatures || [] : [];
  const places = labels.map(label => ({
    geometry: { type: 'Point', coordinates: label.coordinates || [0, 0] },
    id: label.id || '',
    name: label.name || '',
    kind: label.kind || 'custom',
    country_id: label.countryId || label.country_id || '',
    notes: label.notes || '',
  }));
  const genericFeatureRow = item => ({ geometry: item.geometry, id: item.id || '', name: item.properties?.name || '', role: item.properties?.role || 'generic', owner_id: item.properties?.ownerId || '', parent_id: item.properties?.parentId || '', topology_group: item.properties?.topologyGroup || '', land_binding: item.properties?.landBinding || 'none', color: item.properties?.color || '', notes: item.properties?.notes || '', locked: item.properties?.locked === true ? 1 : 0, properties_json: JSON.stringify(item.properties || {}) });
  const points = genericFeatures.filter(item => ['Point', 'MultiPoint'].includes(item.geometry?.type)).map(genericFeatureRow);
  const lines = genericFeatures.filter(item => ['LineString', 'MultiLineString'].includes(item.geometry?.type)).map(genericFeatureRow);
  const polygons = genericFeatures.filter(item => ['Polygon', 'MultiPolygon'].includes(item.geometry?.type)).map(genericFeatureRow);
  const territorialRows = self.PandoLabGisAdapters.territorialRows(state);
  const distributionRows = self.PandoLabGisAdapters.distributionRows(state);
  const genericFeatureColumns = [
    { name: 'id' }, { name: 'name' }, { name: 'role' }, { name: 'owner_id' }, { name: 'parent_id' },
    { name: 'topology_group' }, { name: 'land_binding' }, { name: 'color' }, { name: 'notes' }, { name: 'locked', type: 'INTEGER' }, { name: 'properties_json' },
  ];
  if (gisMode && includes('countries')) {
    const overrides = state.countryOverrides || {};
    const countryRows = (state.countriesData?.features || []).map(feature => {
      const properties = feature.properties || {};
      const id = String(feature?.id || '');
      return {
        geometry: feature.geometry,
        pandolab_id: id,
        pandolab_name: overrides[id]?.name || properties.name || id,
        valid_from: properties.validFrom || null,
        valid_to: properties.validTo || null,
      };
    });
    const countryColumns = [
      { name: 'pandolab_id' }, { name: 'pandolab_name' }, { name: 'valid_from' }, { name: 'valid_to' },
    ];
    createFeatureTable(db, { tableName: 'countries', geometryType: 'MULTIPOLYGON', rows: countryRows, columns: countryColumns, description: 'PandoLab GIS countries' });
  }
  if (includes('labels')) createFeatureTable(db, { tableName: 'places', geometryType: 'POINT', rows: places, columns: [{ name: 'pandolab_id', source: 'id' }, { name: 'name' }, { name: 'kind' }, { name: 'country_id' }, { name: 'notes' }], description: 'PandoLab places' });
  if (includes('genericFeatures')) {
    createFeatureTable(db, { tableName: 'generic_features_point', geometryType: 'POINT', rows: points, columns: genericFeatureColumns, description: 'PandoLab point generic features' });
    createFeatureTable(db, { tableName: 'generic_features_line', geometryType: 'MULTILINESTRING', rows: lines, columns: genericFeatureColumns, description: 'PandoLab line generic features' });
    createFeatureTable(db, { tableName: 'generic_features_polygon', geometryType: 'MULTIPOLYGON', rows: polygons, columns: genericFeatureColumns, description: 'PandoLab polygon generic features' });
  }
  const territorialColumns = [
    { name: 'id' }, { name: 'name' }, { name: 'type' }, { name: 'parent_id' }, { name: 'sovereign_id' },
    { name: 'valid_from' }, { name: 'valid_to' },
    { name: 'color' }, { name: 'style_key' }, { name: 'source_library_id' }, { name: 'source_geometry_version' },
    { name: 'metadata_json' }, { name: 'properties_json' },
  ];
  for (const [unitType, tableName] of Object.entries(self.PandoLabGisAdapters.TERRITORIAL_TABLES)) {
    const logicalLayer = unitType === 'subunit' ? 'subunits' : 'regions';
    if (!includes(logicalLayer)) continue;
    createFeatureTable(db, { tableName, geometryType: 'MULTIPOLYGON', rows: territorialRows[tableName] || [], columns: territorialColumns, description: `PandoLab ${unitType} territorial units` });
  }
  const distributionColumns = [
    { name: 'entry_id' }, { name: 'layer_id' }, { name: 'name' }, { name: 'distribution_type' },
    { name: 'parent_layer_id' }, { name: 'color' }, { name: 'layer_visible', type: 'INTEGER' }, { name: 'layer_locked', type: 'INTEGER' },
    { name: 'source_mode' }, { name: 'territorial_unit_id' }, { name: 'share', type: 'REAL' }, { name: 'certainty' },
    { name: 'valid_from' }, { name: 'valid_to' }, { name: 'layer_metadata_json' }, { name: 'entry_metadata_json' },
  ];
  if (includes('distributions')) {
    for (const [distributionType, tableName] of Object.entries(self.PandoLabGisAdapters.DISTRIBUTION_TABLES)) {
      createFeatureTable(db, { tableName, geometryType: 'MULTIPOLYGON', rows: distributionRows[tableName] || [], columns: distributionColumns, description: `PandoLab ${distributionType} distribution entries` });
    }
  }

  if (gisMode) return;
  createAttributeTable(db, 'pandolab_project_settings', 'setting_key TEXT PRIMARY KEY NOT NULL, json_value TEXT NOT NULL', 'PandoLab project settings');
  const settings = { ...state };
  delete settings.countriesData;
  delete settings.countryAssets;
  delete settings.hydroCollections;
  db.run('INSERT INTO pandolab_project_settings (setting_key, json_value) VALUES (?, ?)', ['project_state', JSON.stringify(settings)]);

  createAttributeTable(db, 'pandolab_country_assets', 'country_id TEXT PRIMARY KEY NOT NULL, mime_type TEXT NOT NULL, image_data BLOB NOT NULL', 'PandoLab country flag assets');
  const assetInsert = db.prepare('INSERT INTO pandolab_country_assets (country_id, mime_type, image_data) VALUES (?, ?, ?)');
  for (const asset of state.countryAssets || []) {
    if (!asset?.countryId || !asset?.base64) continue;
    assetInsert.run([String(asset.countryId), String(asset.mimeType || 'application/octet-stream'), base64ToBytes(asset.base64)]);
  }
  assetInsert.free();

  createAttributeTable(db, 'pandolab_source_info', 'info_key TEXT PRIMARY KEY NOT NULL, json_value TEXT NOT NULL', 'PandoLab source provenance');
  db.run('INSERT INTO pandolab_source_info (info_key, json_value) VALUES (?, ?)', ['source', JSON.stringify(state.sourceInfo || {})]);
}

async function writeGeoPackage(buffer, projectState, options = {}) {
  const SQL = await getSql();
  const db = new SQL.Database(new Uint8Array(buffer));
  try {
    db.run('BEGIN IMMEDIATE');
    writeAtlasTables(db, { projectState, ...options });
    db.run('COMMIT');
    return db.export();
  } catch (error) {
    try { db.run('ROLLBACK'); } catch (_) {}
    throw error;
  } finally {
    db.close();
  }
}

function tableExists(db, tableName) {
  const statement = db.prepare("SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1");
  statement.bind([tableName]);
  const exists = statement.step();
  statement.free();
  return exists;
}

async function readAtlasTables(buffer) {
  const SQL = await getSql();
  const db = new SQL.Database(new Uint8Array(buffer));
  try {
    const result = { projectState: null, countryAssets: [], sourceInfo: null };
    if (tableExists(db, 'pandolab_project_settings')) {
      const rows = db.exec("SELECT json_value FROM pandolab_project_settings WHERE setting_key='project_state' LIMIT 1");
      if (rows[0]?.values?.[0]?.[0]) result.projectState = JSON.parse(rows[0].values[0][0]);
    }
    if (tableExists(db, 'pandolab_country_assets')) {
      const statement = db.prepare('SELECT country_id, mime_type, image_data FROM pandolab_country_assets');
      while (statement.step()) {
        const row = statement.get();
        result.countryAssets.push({ countryId: String(row[0]), mimeType: String(row[1]), base64: bytesToBase64(new Uint8Array(row[2])) });
      }
      statement.free();
    }
    if (tableExists(db, 'pandolab_source_info')) {
      const rows = db.exec("SELECT json_value FROM pandolab_source_info WHERE info_key='source' LIMIT 1");
      if (rows[0]?.values?.[0]?.[0]) result.sourceInfo = JSON.parse(rows[0].values[0][0]);
    }
    return result;
  } finally {
    db.close();
  }
}

self.onmessage = async event => {
  const { id, action, buffer, projectState, exportMode, selectedLayers } = event.data || {};
  try {
    if (action === 'write') {
      const output = await writeGeoPackage(buffer, projectState, { exportMode, selectedLayers });
      self.postMessage({ id, ok: true, buffer: output.buffer }, [output.buffer]);
      return;
    }
    if (action === 'read') {
      const metadata = await readAtlasTables(buffer);
      self.postMessage({ id, ok: true, metadata });
      return;
    }
    throw new Error('알 수 없는 GeoPackage Worker 작업입니다.');
  } catch (error) {
    self.postMessage({ id, ok: false, error: error?.message || String(error) });
  }
};
