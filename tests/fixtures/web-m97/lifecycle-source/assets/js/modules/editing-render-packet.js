const immutableGeometries = new WeakSet();

/** Only call for privately owned calculation results, never live project geometry. */
export function freezeEditingGeometry(value) {
  if (!value || immutableGeometries.has(value)) return value;
  const freezeCoordinates = coordinates => {
    if (!Array.isArray(coordinates) || Object.isFrozen(coordinates)) return;
    for (const child of coordinates) freezeCoordinates(child);
    Object.freeze(coordinates);
  };
  freezeCoordinates(value.coordinates);
  Object.freeze(value);
  immutableGeometries.add(value);
  return value;
}

const coordinate = value => (
  Array.isArray(value) && value.length >= 2
    ? Object.freeze([Number(value[0]), Number(value[1])])
    : null
);

const coordinates = values => Object.freeze((values || []).map(coordinate).filter(Boolean));

const cloneFrozen = value => {
  if (Array.isArray(value)) return Object.freeze(value.map(cloneFrozen));
  if (!value || typeof value !== 'object') return value;
  if (immutableGeometries.has(value)) return value;
  const output = {};
  for (const [key, item] of Object.entries(value)) output[key] = cloneFrozen(item);
  return Object.freeze(output);
};

const cloneGeometryCoordinates = value => {
  if (!Array.isArray(value)) return value;
  if (value.length >= 2 && Number.isFinite(Number(value[0])) && Number.isFinite(Number(value[1]))) {
    return coordinate(value);
  }
  return Object.freeze(value.map(cloneGeometryCoordinates));
};

const geometry = value => {
  if (!value?.type) return null;
  if (immutableGeometries.has(value)) return value;
  return Object.freeze({
    type: String(value.type),
    coordinates: cloneGeometryCoordinates(value.coordinates),
  });
};

const ref = value => {
  if (!value?.domain || value.id === undefined || value.id === null) return null;
  return Object.freeze({
    domain: String(value.domain),
    type: String(value.type || value.domain),
    id: String(value.id),
    key: String(value.key || `${value.domain}:${value.type || value.domain}:${encodeURIComponent(String(value.id))}`),
  });
};

const issue = value => Object.freeze({
  ...cloneFrozen(value || {}),
  id: String(value?.id || ''),
  kind: String(value?.kind || 'invalid'),
  message: String(value?.message || ''),
  vertexIndex: Number.isInteger(value?.vertexIndex) ? value.vertexIndex : null,
  segmentIndex: Number.isInteger(value?.segmentIndex) ? value.segmentIndex : null,
  coordinate: coordinate(value?.coordinate),
  geometry: geometry(value?.geometry),
});

const vertex = value => Object.freeze({
  key: String(value?.key || value?.nodeKey || value?.index || ''),
  nodeKey: value?.nodeKey ? String(value.nodeKey) : null,
  index: Number.isInteger(value?.index) ? value.index : null,
  partIndex: Number.isInteger(value?.partIndex) ? value.partIndex : null,
  polygonIndex: Number.isInteger(value?.polygonIndex) ? value.polygonIndex : null,
  ringIndex: Number.isInteger(value?.ringIndex) ? value.ringIndex : null,
  coordinate: coordinate(value?.coordinate || value?.coord),
  selected: value?.selected === true,
  fixed: value?.fixed === true,
  boundaryKind: value?.boundaryKind ? String(value.boundaryKind) : null,
  ownerIds: Object.freeze([...(value?.ownerIds || [])].map(String)),
});

const segment = value => Object.freeze({
  key: String(value?.key || value?.segmentIndex || ''),
  segmentIndex: Number.isInteger(value?.segmentIndex) ? value.segmentIndex : null,
  start: coordinate(value?.start || value?.a),
  end: coordinate(value?.end || value?.b),
  kind: value?.kind ? String(value.kind) : null,
  selected: value?.selected === true,
});

const freezeList = (values, mapper = value => Object.freeze({ ...value })) => Object.freeze((values || []).map(mapper));

export function createDraftRenderPacket(input = {}) {
  const insertTarget = input.insertTarget?.coordinate ? Object.freeze({
    segmentIndex: Number(input.insertTarget.segmentIndex),
    coordinate: coordinate(input.insertTarget.coordinate),
  }) : null;
  return Object.freeze({
    active: input.active === true,
    inputPhase: input.inputPhase === 'refine' ? 'refine' : 'draw',
    vertexInsertMode: input.vertexInsertMode === true,
    shape: input.shape === 'polygon' ? 'polygon' : input.shape === 'line' ? 'line' : null,
    geometry: geometry(input.geometry),
    rawStrokeGeometry: geometry(input.rawStrokeGeometry),
    autoCloseSegment: input.autoCloseSegment ? segment(input.autoCloseSegment) : null,
    vertices: freezeList(input.vertices, vertex),
    segments: freezeList(input.segments, segment),
    selectedVertexIndex: Number.isInteger(input.selectedVertexIndex) ? input.selectedVertexIndex : null,
    insertTarget,
    dragging: input.dragging === true,
    issues: freezeList(input.issues, issue),
    splitCandidates: freezeList(input.splitCandidates, (value, index) => Object.freeze({
      key: String(value?.key || `split:${index}`),
      geometry: geometry(value?.geometry),
      area: Number(value?.area || value?.areaKm2 || 0),
    })),
    snapPoints: freezeList(input.snapPoints, value => Object.freeze({
      endpoint: String(value?.endpoint || ''),
      kind: String(value?.kind || ''),
      coordinate: coordinate(value?.coordinate),
    })),
    cutStatus: input.cutStatus ? String(input.cutStatus) : null,
    canCommit: input.canCommit === true,
  });
}

const objectVerticesPacket = input => {
  if (!input) return null;
  return Object.freeze({
    targetRef: ref(input.targetRef),
    mode: String(input.mode || ''),
    handles: freezeList(input.handles, vertex),
    previewSegments: freezeList(input.previewSegments, segment),
  });
};

const trustedBoundaries = new WeakSet();
/** Adopt a privately owned Worker result once; packets share its original coordinates. */
export function adoptBoundaryRenderPacket(input) {
  if (!input || trustedBoundaries.has(input)) return input;
  const seen = new WeakSet();
  const freeze = value => {
    if (!value || typeof value !== 'object' || seen.has(value)) return;
    seen.add(value);
    for (const child of Object.values(value)) freeze(child);
    Object.freeze(value);
  };
  freeze(input);
  trustedBoundaries.add(input);
  return input;
}
/** Large Worker results are adopted in bounded UI turns so cancellation stays responsive. */
export async function adoptBoundaryRenderPacketAsync(input, checkpoint) {
  if (!input || trustedBoundaries.has(input)) return input;
  const seen = new WeakSet(), stack = [input];
  let count = 0;
  while (stack.length) {
    const value = stack.pop();
    if (!value || typeof value !== 'object' || seen.has(value)) continue;
    seen.add(value);
    for (const child of Object.values(value)) if (child && typeof child === 'object') stack.push(child);
    Object.freeze(value);
    if (++count % 512 === 0) await checkpoint();
  }
  trustedBoundaries.add(input);
  return input;
}
const boundaryPacket = input => {
  if (!input) return null;
  if (trustedBoundaries.has(input)) return input;
  return Object.freeze({
    preparationId: String(input.preparationId || ''),
    segments: freezeList(input.segments, segment),
    handles: freezeList(input.handles, vertex),
  });
};

const territoryPacket = input => {
  if (!input) return null;
  return Object.freeze({
    kind: String(input.kind || ''),
    sourceKey: String(input.sourceKey || ''),
    phase: String(input.phase || ''),
    components: freezeList(input.components, value => Object.freeze({
      key: String(value?.key || ''),
      geometry: geometry(value?.geometry),
      selected: value?.selected === true,
      hovered: value?.hovered === true,
      countryName: String(value?.countryName || ''),
      areaKm2: Number(value?.areaKm2 || 0),
      usesRiverBoundary: value?.usesRiverBoundary === true,
      riverBoundarySegments: Object.freeze((value?.riverBoundarySegments || []).map(coordinates)),
    })),
    candidates: freezeList(input.candidates, (value, position) => Object.freeze({
      key: String(value?.key || `candidate:${position}`),
      id: value?.id ? String(value.id) : null,
      index: Number(value?.index || 0),
      geometry: geometry(value?.geometry),
      selected: value?.selected === true,
      interactive: value?.interactive !== false,
    })),
    riverBoundarySegments: Object.freeze((input.riverBoundarySegments || []).map(coordinates)),
  });
};

const snapPacket = input => {
  if (!input?.coordinate) return null;
  return Object.freeze({
    kind: String(input.kind || ''),
    coordinate: coordinate(input.coordinate),
    segmentEndpoints: input.segmentEndpoints ? coordinates(input.segmentEndpoints) : null,
    ownerIds: Object.freeze([...(input.ownerIds || [])].map(String)),
    nodeKey: input.nodeKey ? String(input.nodeKey) : null,
    segmentKey: input.segmentKey ? String(input.segmentKey) : null,
  });
};

export function createEditingRenderPacket(input = {}) {
  return Object.freeze({
    version: 1,
    revision: Number(input.revision || 0),
    projectGeneration: Number(input.projectGeneration || 0),
    tool: String(input.tool || 'select'),
    phase: String(input.phase || 'idle'),
    draft: createDraftRenderPacket(input.draft),
    objectVertices: objectVerticesPacket(input.objectVertices),
    boundaryEdit: boundaryPacket(input.boundaryEdit),
    boundaryActiveNodeKey: input.boundaryActiveNodeKey || null,
    boundaryActiveCoordinate: coordinate(input.boundaryActiveCoordinate),
    territoryOperation: territoryPacket(input.territoryOperation),
    snap: snapPacket(input.snap),
    preview: input.preview ? cloneFrozen(input.preview) : null,
    validationIssues: freezeList(input.validationIssues, issue),
  });
}

export const EMPTY_EDITING_RENDER_PACKET = createEditingRenderPacket();
