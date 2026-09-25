// Run the pinned browser GeoPackage worker with a SQLite-compatible Node shim.
// The worker owns table layouts and geometry bytes; the shim only supplies SQL.
import { readFileSync, writeFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { DatabaseSync } from 'node:sqlite';
import { runInNewContext } from 'node:vm';

const root = new URL('../tests/fixtures/web-m6/', import.meta.url);
const source = new URL('source/', root);
const pin = JSON.parse(readFileSync(new URL('manifest.json', root)));
for (const name of ['gis-gpkg-worker.js', 'gis-adapters.js']) {
  const body = readFileSync(new URL(name, source));
  const sha = createHash('sha1').update('blob ' + body.length + '\0').update(body).digest('hex');
  if (sha !== pin.sourceBlobs[name]) throw new Error('Web worker source mismatch: ' + name);
}
const bind = values => (values || []).map(value => value instanceof Uint8Array ? Buffer.from(value) : value);
class SqlDatabase {
  constructor(bytes) {
    this.db = new DatabaseSync(':memory:');
    this.db.deserialize(Buffer.from(bytes));
  }
  run(sql, values) {
    if (values) this.db.prepare(sql).run(...bind(values));
    else this.db.exec(sql);
  }
  prepare(sql) {
    const statement = this.db.prepare(sql);
    let iterator;
    let current;
    return {
      run: values => statement.run(...bind(values)),
      bind: values => { iterator = statement.iterate(...bind(values)); },
      step: () => { const next = iterator.next(); current = next.value; return !next.done; },
      get: () => Object.values(current),
      free: () => {},
    };
  }
  exec(sql) {
    const statement = this.db.prepare(sql);
    return [{ columns: statement.columns().map(c => c.name),
      values: statement.all().map(row => Object.values(row)) }];
  }
  export() { return new Uint8Array(this.db.serialize()); }
  close() { this.db.close(); }
}
const context = {
  URL, Uint8Array, ArrayBuffer, DataView, Buffer, console, structuredClone,
  atob: value => Buffer.from(value, 'base64').toString('binary'),
  btoa: value => Buffer.from(value, 'binary').toString('base64'),
  location: { href: 'https://example.invalid/assets/js/workers/gis-gpkg-worker.js' },
  Date: class extends Date {
    constructor(...args) { super(...(args.length ? args : ['2026-01-01T00:00:00.000Z'])); }
    static now() { return 1767225600000; }
  },
};
context.self = context;
context.initSqlJs = async () => ({ Database: SqlDatabase });
context.importScripts = url => {
  if (url.includes('gis-adapters.js')) runInNewContext(
    readFileSync(new URL('gis-adapters.js', source), 'utf8'), context, { filename: 'gis-adapters.js' });
};
runInNewContext(readFileSync(new URL('gis-gpkg-worker.js', source), 'utf8'), context,
  { filename: 'gis-gpkg-worker.js' });

const seed = new DatabaseSync(':memory:');
seed.exec(`
 PRAGMA application_id=1196444487;
 PRAGMA user_version=10300;
 CREATE TABLE gpkg_spatial_ref_sys (
   srs_name TEXT NOT NULL, srs_id INTEGER NOT NULL PRIMARY KEY,
   organization TEXT NOT NULL, organization_coordsys_id INTEGER NOT NULL,
   definition TEXT NOT NULL, description TEXT);
 INSERT INTO gpkg_spatial_ref_sys VALUES
   ('WGS 84',4326,'EPSG',4326,'GEOGCS["WGS 84"]',''),
   ('Undefined Cartesian',-1,'NONE',-1,'undefined',''),
   ('Undefined geographic',0,'NONE',0,'undefined','');
 CREATE TABLE gpkg_contents (
   table_name TEXT NOT NULL PRIMARY KEY, data_type TEXT NOT NULL,
   identifier TEXT UNIQUE, description TEXT DEFAULT '',
   last_change DATETIME NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),
   min_x DOUBLE, min_y DOUBLE, max_x DOUBLE, max_y DOUBLE, srs_id INTEGER);
 CREATE TABLE gpkg_geometry_columns (
   table_name TEXT NOT NULL, column_name TEXT NOT NULL, geometry_type_name TEXT NOT NULL,
   srs_id INTEGER NOT NULL, z TINYINT NOT NULL, m TINYINT NOT NULL,
   PRIMARY KEY(table_name,column_name));
`);
const base = seed.serialize();
seed.close();

const square = x => ({ type: 'Polygon',
  coordinates: [[[x,0],[x+2,0],[x+2,2],[x,2],[x,0]]] });
const state = {
  countriesData: { features: [{ type: 'Feature', id: 'AAA',
    properties: { name: 'Alpha' }, geometry: square(0) }] },
  countryOverrides: { AAA: { flagDataUrl: null } },
  territorialUnits: [{ id: 'sub:1', geometry: square(0), properties: {
    unitType: 'subunit', name: 'Subdivision', sovereignId: 'AAA', parentId: 'AAA',
    sourceLibraryId: 'history:1', style: { color: '#123456' },
  } }],
  genericFeatures: [{ type: 'Feature', id: 'generic:1',
    properties: { name: 'Generic' }, geometry: { type: 'Point', coordinates: [1,1] } }],
  distributionLayers: [{ id: 'lang:1', type: 'language', name: 'Language', color: '#0088ff' }],
  distributionEntries: [{ id: 'entry:1', layerId: 'lang:1', mode: 'territorial',
    territorialUnitId: 'AAA', share: 60, certainty: 'known' }],
  labels: [{ id: 'label:1', name: 'Capital', coordinates: [1,1] }],
  countryAssets: [{ countryId: 'AAA', mimeType: 'image/svg+xml',
    base64: Buffer.from('<svg xmlns="http://www.w3.org/2000/svg"/>').toString('base64') }],
  sourceInfo: { fixture: 'web-worker' },
};
let response;
context.postMessage = value => { response = value; };
async function write(bytes, mode, selectedLayers) {
  response = undefined;
  await context.onmessage({ data: {
    id: 1, action: 'write', buffer: Uint8Array.from(bytes).buffer,
    projectState: state, exportMode: mode, selectedLayers,
  } });
  if (!response?.ok) throw new Error(response?.error || 'worker produced no result');
  return Buffer.from(response.buffer);
}
const gis = await write(base, 'gis',
  ['countries','subunits','regions','genericFeatures','distributions','labels']);
const project = await write(gis, 'project', []);
for (const [name, bytes] of [['web-gis.gpkg', gis], ['web-project.gpkg', project]]) {
  const path = new URL(name, root);
  if (process.argv.includes('--verify')) {
    const original = readFileSync(path);
    const oldDb = new DatabaseSync(':memory:');
    const newDb = new DatabaseSync(':memory:');
    oldDb.deserialize(original);newDb.deserialize(bytes);
    const digest = db => {
      const names = db.prepare("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name").all()
        .map(x => x.name).filter(x => !x.startsWith('sqlite_'));
      return names.map(table => [table,
        db.prepare('SELECT * FROM "' + table + '" ORDER BY rowid').all().map(row =>
          Object.values(row).map(v => Buffer.isBuffer(v) ? v.toString('hex') : v))]);
    };
    if (JSON.stringify(digest(oldDb)) !== JSON.stringify(digest(newDb)))
      throw new Error('Web GeoPackage fixture content changed: ' + name);
    oldDb.close();newDb.close();
  } else writeFileSync(path, bytes);
}
console.log(JSON.stringify({ gisBytes: gis.length, projectBytes: project.length }));
