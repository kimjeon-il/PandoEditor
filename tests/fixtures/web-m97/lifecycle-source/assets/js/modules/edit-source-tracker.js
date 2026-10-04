import { geometryRevision as revisionOfGeometry } from './geometry-versions.js';
// Geometry is transferred only when its reference or explicit revision changes.
// Metadata changes travel separately, without serializing coordinate arrays.
export function createEditSourceTracker() {
  let sources = new Map();
  let revision = 0;
  function update(rows) {
    const next = new Map(), patches = [], removedKeys = [];
    let calculationChanged = false;
    for (const { kind, feature, geometryRevision = revisionOfGeometry(feature.geometry) } of rows) {
      const key = `${kind}:${feature.id}`;
      const { geometry, ...metadata } = feature;
      const signature = JSON.stringify(metadata);
      const semantic = JSON.stringify([kind, feature.properties?.entityKind, feature.properties?.parentId, undefined, feature.properties?.locked === true, feature.properties?.ownerId, feature.properties?.landBinding]);
      const previous = sources.get(key);
      const shapeChanged = !previous || previous.geometry !== geometry || previous.geometryRevision !== geometryRevision;
      if (shapeChanged || previous?.semantic !== semantic) calculationChanged = true;
      if (shapeChanged || previous.signature !== signature) {
        patches.push({ key, kind, metadata, ...(shapeChanged ? { geometry } : {}) });
      }
      next.set(key, { geometry, geometryRevision, signature, semantic });
    }
    for (const key of sources.keys()) if (!next.has(key)) removedKeys.push(key);
    sources = next;
    if (calculationChanged || removedKeys.length) revision += 1;
    return { patches, removedKeys, sourceRevision: revision };
  }
  return Object.freeze({ update, reset: () => { sources = new Map(); revision += 1; }, revision: () => revision });
}
